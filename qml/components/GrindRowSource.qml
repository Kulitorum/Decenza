import QtQuick
import Decenza

// Non-visual candidate/step source for grind + RPM dial-in editing
// (replace-grind-inputs-with-picker). Extracted from GrindQuickSelectItem so
// every surface that edits a grind value — brew bar, Brew Settings, post-shot
// review, bag form, recipe wizard — generates candidates the same way.
//
// CONTEXT INJECTION: the grinder is a property of the VALUE being edited, not
// of the application. The host supplies the grinder that owns the value (the
// shot's grinder in post-shot review, the recipe's package in the wizard, the
// bag's equipment in the beans dialog, the active grinder on the brew bar) and
// every derived behaviour — step size, notation, observed-history fallback,
// RPM capability — resolves against it. Nothing here reads the active grinder.
// (The pre-split _observedFallback read Settings.dye.dyeGrinderModel — the
// active grinder — which was harmless while the brew bar was the only host and
// is exactly the bug this injection exists to prevent.)
//
// Per-candidate stepping is catalog-first via SettingsDye.stepGrinderSetting
// (numeric AND Compound "a+b" notation), then plain-numeric / number-in-text /
// letter fallbacks. When that lattice collapses, grindRowsFor's last-resort
// order is: for an EMPTY value, a median-anchored wide window (_medianObserved-
// Anchor -> _windowAround, #1605); otherwise the observed-history fallback.
// The negative-candidate semantics and the window width did change in this
// change: negatives now generate freely on plain-numeric grinders (a stepless
// collar's zero is a user-set calibration reference), while click-indexed
// (Compound) grinders skip them in both the catalog path and the JS fallback.
QtObject {
    id: root

    // --- Injected context ---
    property string grinderBrand: ""
    property string grinderModel: ""
    // RPM capability: one function, called with the injected identity — never a
    // stored per-package flag, never the active grinder (grind-value-entry).
    readonly property bool rpmCapable:
        Settings.dye.grinderRpmCapable(root.grinderBrand, root.grinderModel)

    // Bumped when the async distinct-value cache refreshes, so derived steps and
    // the history fallback re-evaluate once shot history finishes loading.
    property int distinctCacheVersion: 0
    readonly property Connections _historyConn: Connections {
        target: MainController.shotHistory
        function onDistinctCacheReady() { root.distinctCacheVersion++ }
    }

    // Per-grinder grind step, derived from the user's own shot history for the
    // INJECTED grinder by the same noise-filtered estimator the AI dialing
    // context uses. Falls back to 1.0 when history is too thin or the cache is
    // cold. With no grinder, grindStepForGrinder("") derives from full history.
    readonly property double grindStep: {
        var __ = root.distinctCacheVersion
        var s = MainController.shotHistory
            ? MainController.shotHistory.grindStepForGrinder(root.grinderModel) : 0
        return s > 0 ? s : 1.0
    }

    // RPM step from the injected grinder's observed RPMs; the 50 default keeps
    // adjacent rows a meaningful ~50 RPM apart across the ~600–1400 working
    // range.
    readonly property int rpmStep: {
        var __ = root.distinctCacheVersion
        var s = MainController.shotHistory
            ? MainController.shotHistory.grindRpmStepForGrinder(root.grinderModel) : 0
        return s > 0 ? Math.round(s) : 50
    }

    // Neutral anchor when the RPM is unset (0): the RPM wheel centres here so it
    // is adjustable on the first tap. Only a seed — nothing written until picked.
    readonly property int rpmDefaultAnchor: 1000

    // Wheel window half-width, in steps. Deliberately far beyond any physical
    // dial so spinning is effectively unbounded — the user must never have to
    // close and reopen the picker to keep going (a Niche 9 -> -1 move is 40
    // steps at 0.25). The REAL limits are semantic and live in the stepper:
    // click-indexed grinders floor at 0, letters clamp A..Z. Only ~5 rows are
    // visible at a time, so a wide window costs nothing to look at.
    readonly property int grindWindowSteps: 400
    readonly property int rpmWindowSteps: 40

    // --- Stepping algorithm -------------------------------------------------
    function _stepDecimals(step) {
        // Decimal places come from the STEP, not the current value:
        // 1.0 -> 0 ("31"), 0.5 -> 1 ("30.5"), 0.25 -> 2. Sanitize float-dirty
        // steps to 3 decimals and strip trailing zeros so labels + the
        // persisted value stay clean (the picked label string is written
        // straight through to the host's store).
        var s = Number(step).toFixed(3).replace(/0+$/, "").replace(/\.$/, "")
        var dot = s.indexOf(".")
        return dot < 0 ? 0 : (s.length - dot - 1)
    }

    function _fmtNum(v, step) {
        return v.toFixed(_stepDecimals(step))
    }

    // Return the grind setting `n` steps from `currentString`,
    // or "" to skip (unparseable / below a click-indexed dial floor).
    function stepGrind(currentString, n, step) {
        var s = String(currentString == null ? "" : currentString).trim()
        if (s.length === 0)
            return ""

        // 0. Catalog-first: registry grinders (numeric AND compound rotation
        //    "a+b") step through the notation-aware pipeline. Returns "" for a
        //    custom grinder, an unparseable value, or a click-indexed
        //    below-floor candidate — then the JS branches take over.
        var viaCatalog = Settings.dye.stepGrinderSetting(root.grinderBrand, root.grinderModel,
                                                         s, n * step, _stepDecimals(step))
        if (viaCatalog && viaCatalog.length > 0)
            return viaCatalog

        // 1. Pure numeric. No general below-zero skip: a stepless collar's
        //    zero is a user-set calibration reference (Niche Zero), and
        //    dialling finer than zero is a real operation — the regex already
        //    accepts a leading "-". Click-indexed (Compound) grinders are the
        //    exception in BOTH paths: stepGrinderSetting returns "" for their
        //    below-floor candidates, but that "" falls through to THIS branch
        //    (a Mignon user logging plain "2.5" lands here), so the skip must
        //    be re-checked or the catalog's refusal is silently resurrected.
        if (/^-?\d+(\.\d+)?$/.test(s)) {
            var v = parseFloat(s) + n * step
            if (v < 0 && Settings.dye.grinderIsClickIndexed(root.grinderBrand, root.grinderModel))
                return ""             // click-indexed dial floor -> skip
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

    // --- Row generation -----------------------------------------------------
    // Observed-history fallback: the full list of the injected grinder's
    // observed settings, with the current value's slot flagged isCurrent (or
    // cur prepended when it isn't in history). Reached when the wheel can build
    // no lattice — an unparseable current value, or an empty value on a grinder
    // with no numeric history for the median anchor. NOT capped: showing all
    // observed settings is strictly better than truncating to ~10 (#1605).
    function _observedFallback(cur) {
        var out = []
        var model = String(root.grinderModel || "")
        var observed = (MainController.shotHistory && model.length > 0)
            ? MainController.shotHistory.getDistinctGrinderSettingsForGrinder(model)
            : []
        if (!observed || observed.length === 0) {
            if (cur.length > 0)
                out.push({ value: cur, isCurrent: true })
            return out
        }
        var list = observed.slice()
        var idx = list.indexOf(cur)
        if (idx < 0) {
            // Current not in history: prepend it (when set — an empty current
            // must not become a blank highlighted row), then ALL observed.
            if (cur.length > 0)
                out.push({ value: cur, isCurrent: true })
            for (var k = 0; k < list.length; k++)
                out.push({ value: list[k], isCurrent: false })
            return out
        }
        for (var i = 0; i < list.length; i++)
            out.push({ value: list[i], isCurrent: i === idx })
        return out
    }

    // Median of the injected grinder's observed NUMERIC settings, as a string,
    // or "" when the grinder has no numeric history. This anchors the wide
    // wheel when the picker opens on an EMPTY grind (#1605): a new recipe with
    // no dialled grind must still spin a full range, not the ~10 observed
    // values. The numeric subset keeps a stray text setting ("medium") from
    // skewing the anchor; the median lands the wheel in the middle of the user's
    // own range. A grinder whose history is ALL compound ("a+b") notation yields
    // no numeric subset and returns "" — such a grinder keeps the observed
    // fallback / text mode on an empty grind rather than a synthesised window.
    function _medianObservedAnchor() {
        var model = String(root.grinderModel || "")
        var observed = (MainController.shotHistory && model.length > 0)
            ? MainController.shotHistory.getDistinctGrinderSettingsForGrinder(model)
            : []
        if (!observed || observed.length === 0)
            return ""
        var nums = []
        for (var i = 0; i < observed.length; i++) {
            var t = String(observed[i]).trim()
            if (/^-?\d+(\.\d+)?$/.test(t))
                nums.push(parseFloat(t))
        }
        if (nums.length === 0)
            return ""
        nums.sort(function(a, b) { return a - b })
        return String(nums[Math.floor(nums.length / 2)])
    }

    // Wide window centred on an arbitrary anchor string, deduped, with the
    // anchor's canonical row flagged isCurrent so _centerWheels lands on it.
    // May return a short (or empty) array when the anchor seeds few rows; the
    // caller (grindRowsFor) treats <= 2 rows as failure and falls through.
    function _windowAround(anchor, step) {
        var canon = root.stepGrind(anchor, 0, step)
        var out = []
        var seen = ({})
        for (var n = -root.grindWindowSteps; n <= root.grindWindowSteps; n++) {
            var v = root.stepGrind(anchor, n, step)
            if (v === "" || v === undefined) continue
            if (seen[v]) continue
            seen[v] = true
            out.push({ value: v, isCurrent: v === canon })
        }
        return out
    }

    // Candidate rows around an arbitrary value — the picker calls this with its
    // PENDING value when re-seeding after typed entry, so the wheel rebases on
    // what the user typed rather than snapping back to the old lattice.
    function grindRowsFor(cur) {
        cur = String(cur == null ? "" : cur).trim()
        var step = root.grindStep
        // Canonical current = the value reformatted to the step's decimals
        // (exactly what n === 0 produces); highlight whichever surviving row
        // equals it so clamp-edge dedup can't lose the highlight.
        var canonicalCurrent = root.stepGrind(cur, 0, step)
        var generated = []
        var seen = ({})
        for (var n = -root.grindWindowSteps; n <= root.grindWindowSteps; n++) {
            var v = root.stepGrind(cur, n, step)
            if (v === "" || v === undefined) continue
            if (seen[v]) continue
            seen[v] = true
            generated.push({ value: v, isCurrent: v === canonicalCurrent })
        }
        if (generated.length <= 2) {
            // The value seeds no lattice. Two sub-cases:
            //  - EMPTY value (a new recipe with no dialled grind): anchor a WIDE
            //    window on the median observed setting so the wheel spins a full
            //    range centred on the user's own grind, not the ~10 observed
            //    values (#1605). Only when the grinder has no numeric history is
            //    the anchor "" and the window can't be built — then text mode.
            //  - NON-EMPTY but unparseable (free text like "coarse"): keep the
            //    observed-history fallback, which centres the user's OWN value
            //    and re-commits it unchanged on Done. Re-anchoring on the median
            //    here would silently replace a value the user actually set, and
            //    grind has no untouched-anchor commit gate (GrindPickerDialog).
            if (cur.length === 0) {
                var anchor = root._medianObservedAnchor()
                if (anchor.length > 0) {
                    var win = root._windowAround(anchor, step)
                    if (win.length > 2)
                        return win
                }
            }
            return root._observedFallback(cur)
        }
        return generated
    }

    // RPM rows around a base (<= 0 seeds from the neutral anchor, unhighlighted).
    // Pure integers; RPM stays > 0 — a motor has no negative speed, and a 0 row
    // would commit as the explicit-clear sentinel.
    function rpmRowsFor(base) {
        var rpmSet = base > 0
        var anchor = rpmSet ? base : root.rpmDefaultAnchor
        var out = []
        var seen = ({})
        for (var n = -root.rpmWindowSteps; n <= root.rpmWindowSteps; n++) {
            var rpm = anchor + n * root.rpmStep
            if (rpm <= 0) continue
            var v = String(rpm)
            if (seen[v]) continue
            seen[v] = true
            out.push({ value: v, isCurrent: rpmSet && n === 0 })
        }
        return out
    }

    // NOTE: this component exposes no reactive rows property on purpose. An
    // earlier design had `grindRows`/`rpmRows` bindings; the picker must not
    // consume rows reactively, because a rebuild under an open Tumbler resets
    // the view mid-interaction (see GrindPickerDialog's snapshot rationale).
    // Callers take an explicit snapshot via grindRowsFor()/rpmRowsFor() and
    // rebuild at defined moments, listening to distinctCacheVersion for the
    // async warm-up.
}
