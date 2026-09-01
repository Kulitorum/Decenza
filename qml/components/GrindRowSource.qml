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
// every derived behaviour — notation, click-indexing, observed-history
// fallback, RPM capability — resolves against it.
// (The pre-split _observedFallback read Settings.dye.dyeGrinderModel — the
// active grinder — which was harmless while the brew bar was the only host and
// is exactly the bug this injection exists to prevent.)
//
// ONE carve-out, and step size is deliberately absent from the list above
// because of it: when NOTHING is injected, grindStep() and rpmStep() resolve
// the history they measure from against the ACTIVE grinder. That is a source
// of samples, not a semantic. An injected identity is never overridden, and
// notation, click-indexing and RPM capability never leave it. See grindStep().
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

    // Per-grinder grind step, derived from the user's own shot history by the
    // same noise-filtered estimator the AI dialing context uses. Prefers the
    // INJECTED grinder, resolves to the active one when nothing was injected,
    // and ends at 1.0 when no history anywhere is thick enough — the body
    // explains why it is that order and not simply the injected one.
    //
    // FUNCTIONS, not properties, and deliberately so. Each call runs a live query
    // measured at 3.3 ms median / 87 ms worst on a real 18.5 MB database. As eager
    // `readonly property` bindings these evaluated on construction and again on
    // every write, across EVERY live GrindRowSource — the resident
    // GrindQuickSelectItem bar widget plus a GrindField in whatever page or dialog
    // is open — so a taste-slider autosave or a shot save cost several of those
    // queries inline on the main thread. The shot-save one lands as the machine
    // leaves Pouring, while BLE telemetry is still on that thread, which is the
    // tighter-budget case CLAUDE.md names.
    //
    // Their only readers are grindRowsFor() / rpmRowsFor(), which are already
    // explicit snapshots taken at defined moments. Calling the query there means
    // it runs exactly when rows are built and never otherwise, and the answer is
    // current by construction — no counter, no staleness, no burst.
    function grindStep() {
        if (!MainController.shotHistory)
            return 1.0
        // With nothing injected the host has no grinder to name yet — a bag or
        // recipe with no equipment package, or a shot recorded before it had
        // one. The active grinder is the honest guess there. The all-grinders
        // pool that grindStepForGrinder("") returns is NOT: it mixes every
        // grinder's dial resolution into one estimate, so a Niche stepping 0.25
        // and an EK43 stepping 1 produce a step belonging to neither, and
        // nothing on screen says the wheel is not about your grinder. A user
        // with one grinder cannot tell the two apart — the pool IS their
        // grinder — which is why nothing has flagged it.
        //
        // Found while reading the log attached to #1726, where the post-shot
        // review reached this branch with an empty model and pooled. It is not
        // that issue's reported symptom: that one matches the composite-key
        // cache #1725 fixed, and #1726 was filed minutes after #1725 merged, so
        // against a build without it. That match is an inference from the log,
        // not a triaged diagnosis — #1726 is still open.
        //
        // Only the source of HISTORY falls back. Notation, click-indexing and
        // rpmCapable stay on the injected identity: those belong to the grinder
        // that owns the value, and reading the active grinder for them is the
        // bug the context injection exists to prevent (see the header).
        var model = root.grinderModel.length > 0
            ? root.grinderModel
            : String(Settings.dye.dyeGrinderModel || "")
        var s = MainController.shotHistory.grindStepForGrinder(model)
        if (s > 0)
            return s

        // Scoping can find LESS than pooling, not merely something different:
        // the pooled query carries no equipment join, so it counts shots
        // recorded before equipment existed and the scoped one cannot. A user
        // whose history is mostly pre-equipment can have a grinder that derives
        // nothing while the pool derives fine — so returning 1.0 straight from
        // here would REGRESS the single-grinder case this change is otherwise a
        // no-op for, which is most users. Pool as a second attempt instead: a
        // step from the user's own dialling habits beats a blind 1.0 even when
        // it cannot be attributed to one grinder.
        //
        // This is also the only path left that pools from the app, and it costs
        // a second query only when the first derived nothing. The web forms
        // reach the same pooling branch through
        // ShotServer::handleGrindCandidatesApi.
        if (model.length > 0) {
            s = MainController.shotHistory.grindStepForGrinder("")
            if (s > 0)
                return s
        }
        return 1.0
    }

    // RPM step from the injected grinder's observed RPMs; the 50 default keeps
    // adjacent rows a meaningful ~50 RPM apart across the ~600–1400 working
    // range. Gated on rpmCapable: only rpmRowsFor() calls this, and the picker
    // only calls that for an RPM grinder, so querying otherwise is pure waste.
    //
    // That gate does NOT screen out the empty-identity case, and an earlier
    // draft of this comment claimed it did. deriveRpmCapable returns TRUE for a
    // grinder it cannot find in the registry — "not in the table, so show the
    // rpm field" (equipmentstorage.cpp:1709) — and an empty brand+model matches
    // nothing, so rpmCapable is true with nothing injected and the picker does
    // build RPM rows. What actually keeps this from pooling is one layer down:
    // grindRpmStepForGrinder early-returns 0.0 on an empty model
    // (shothistorystorage_queries.cpp). Do not delete that guard on the
    // strength of this gate.
    //
    // So resolve the identity the same way grindStep() does, for the same
    // reason — with nothing injected the honest source is the active grinder,
    // and a blind 50 discards RPMs the user has actually dialled. No pooled
    // second attempt here, unlike grindStep(): the empty model the pool would
    // need is precisely what the guard above refuses, so there is nothing to
    // fall through to.
    function rpmStep() {
        if (!root.rpmCapable || !MainController.shotHistory)
            return 50
        var model = root.grinderModel.length > 0
            ? root.grinderModel
            : String(Settings.dye.dyeGrinderModel || "")
        var s = MainController.shotHistory.grindRpmStepForGrinder(model)
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
    function _decimalsOf(x) {
        // Sanitize float-dirty values to 3 decimals and strip trailing zeros so
        // labels + the persisted value stay clean (the picked label string is
        // written straight through to the host's store).
        var s = Number(x).toFixed(3).replace(/0+$/, "").replace(/\.$/, "")
        var dot = s.indexOf(".")
        return dot < 0 ? 0 : (s.length - dot - 1)
    }

    // Decimal places for a stepped label: the STEP's precision, but never fewer
    // than the VALUE already has.
    //
    // Step alone was #1713. The step is derived from shot history and falls back
    // when history is too thin; a fallback of 1.0 gives 0 decimals, and 0
    // decimals reformats a user's "1.1" to "1". The value was not merely
    // unreachable by stepping — it was destroyed by formatting, so a grind of
    // 1.1 became 1 as soon as the picker rendered it, which is precisely what
    // was reported.
    //
    // Taking the max is what makes the two independent: the step decides how far
    // each row moves, the value decides how much precision must survive being
    // written down. A coarse step may now produce 1.1 -> 2.1 -> 3.1, which is
    // correct — it steps by the step and keeps what the user typed.
    function _stepDecimals(step, currentValue) {
        var d = _decimalsOf(step)
        if (currentValue !== undefined && currentValue !== null) {
            var s = String(currentValue).trim()
            if (/^-?\d+(\.\d+)?$/.test(s))
                d = Math.max(d, _decimalsOf(parseFloat(s)))
        }
        return d
    }

    // `orig` is the value being stepped FROM — its precision must survive, see
    // _stepDecimals(). Callers inside stepGrind() pass the trimmed input string.
    function _fmtNum(v, step, orig) {
        return v.toFixed(_stepDecimals(step, orig))
    }

    // Return the grind setting `n` steps from `currentString`,
    // or "" to skip (unparseable / below a click-indexed dial floor).
    function stepGrind(currentString, n, step, precomputed) {
        var s = String(currentString == null ? "" : currentString).trim()
        if (s.length === 0)
            return ""

        // 0. Catalog-first: registry grinders (numeric AND compound rotation
        //    "a+b") step through the notation-aware pipeline. Returns "" for a
        //    custom grinder, an unparseable value, or a click-indexed
        //    below-floor candidate — then the JS branches take over.
        //
        // `precomputed` is this row's catalog answer when the caller already has
        // it from stepGrinderSettingRange(). Same value, one crossing for the
        // whole wheel instead of 801 — see that function. Undefined means "ask",
        // which is what every single-row caller does.
        var viaCatalog = (precomputed !== undefined)
            ? precomputed
            : Settings.dye.stepGrinderSetting(root.grinderBrand, root.grinderModel,
                                              s, n * step, _stepDecimals(step, s))
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
            return _fmtNum(v, step, s)
        }

        // 2. Number embedded in text: optional non-digit prefix + number + suffix.
        var m = s.match(/^(\D*)(\d+(?:\.\d+)?)(\D*)$/)
        if (m) {
            var nv = parseFloat(m[2]) + n * step
            if (nv < 0) nv = 0               // clamp numeric >= 0
            return m[1] + _fmtNum(nv, step, m[2]) + m[3]
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
        var catalog = Settings.dye.stepGrinderSettingRange(
            root.grinderBrand, root.grinderModel, anchor, step,
            -root.grindWindowSteps, root.grindWindowSteps, root._stepDecimals(step, anchor))
        var canon = root.stepGrind(anchor, 0, step, catalog[root.grindWindowSteps])
        var out = []
        var seen = ({})
        for (var n = -root.grindWindowSteps; n <= root.grindWindowSteps; n++) {
            var v = root.stepGrind(anchor, n, step, catalog[n + root.grindWindowSteps])
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
    // One-entry memo of the last generated row set.
    //
    // The rows are a pure function of (cur, step, grinderBrand, grinderModel):
    // same inputs, same 801 strings. Reopening the picker without changing the
    // value — and the text <-> wheel toggle, which rebuilds — regenerate an
    // identical array, so they are served from here.
    //
    // ONLY the lattice path is stored. Its rows are generated from the key's own
    // four inputs and nothing else, and `step` is re-read from the query on every
    // rebuild, so a step that moves misses by construction.
    //
    // The two history-derived paths are deliberately NOT memoized:
    // _observedFallback() and _medianObservedAnchor() both read
    // getDistinctGrinderSettingsForGrinder(), which is a second input the key does
    // not name. Storing them was a real bug — a grinder with non-numeric settings
    // ("coarse"/"fine") takes the fallback on every open, and recording a new
    // setting does not move `cur`, `step`, brand or model, so the memo kept
    // serving a list the new setting was missing from for the rest of the session.
    // That is the same invalidated-on-the-wrong-axis failure as the distinct-value
    // cache CLAUDE.md records as deleted.
    //
    // The array is only ever read by its consumers, never mutated, so handing back
    // the same object is safe. Returning an identical reference also means QML
    // signals no change, so the Tumbler skips rebuilding a model it already has —
    // measured as assign=0ms on a hit against 3ms on a miss.
    property var _rowsMemoKey: ""
    property var _rowsMemo: null

    function _rowsKeyFor(cur, step) {
        return String(cur) + "\u0000" + String(step) + "\u0000"
             + String(root.grinderBrand) + "\u0000" + String(root.grinderModel)
    }

    function grindRowsFor(cur) {
        // TEMPORARY instrumentation (grind-picker open cost) — see
        // GrindPickerDialog._rebuildRows. Remove with those.
        var _t0 = Date.now()
        cur = String(cur == null ? "" : cur).trim()
        var step = root.grindStep()
        var _tQuery = Date.now()
        var _key = root._rowsKeyFor(cur, step)
        if (root._rowsMemo !== null && root._rowsMemoKey === _key) {
            console.info("[Equipment] grind picker: grindRowsFor step-query="
                         + (_tQuery - _t0) + "ms generate=0ms rows="
                         + root._rowsMemo.length + " (memo hit)")
            return root._rowsMemo
        }
        // Canonical current = the value reformatted to the step's decimals
        // (exactly what n === 0 produces); highlight whichever surviving row
        // equals it so clamp-edge dedup can't lose the highlight.
        // One crossing for the whole wheel. Entry i is row (i - grindWindowSteps);
        // empty entries mean the catalog declined that row and stepGrind falls
        // through to its JS branches, exactly as when it asked per row.
        var catalog = Settings.dye.stepGrinderSettingRange(
            root.grinderBrand, root.grinderModel, cur, step,
            -root.grindWindowSteps, root.grindWindowSteps, root._stepDecimals(step, cur))
        var canonicalCurrent = root.stepGrind(cur, 0, step, catalog[root.grindWindowSteps])
        var generated = []
        var seen = ({})
        for (var n = -root.grindWindowSteps; n <= root.grindWindowSteps; n++) {
            var v = root.stepGrind(cur, n, step, catalog[n + root.grindWindowSteps])
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
            // Not memoized — see the memo declaration. This list comes from shot
            // history, which the key does not name.
            var _fb = root._observedFallback(cur)
            console.info("[Equipment] grind picker: grindRowsFor step-query="
                         + (_tQuery - _t0) + "ms generate=" + (Date.now() - _tQuery)
                         + "ms rows=" + _fb.length + " (observed-fallback, not memoized)")
            return _fb
        }
        root._rowsMemoKey = _key
        root._rowsMemo = generated
        console.info("[Equipment] grind picker: grindRowsFor step-query="
                     + (_tQuery - _t0) + "ms generate=" + (Date.now() - _tQuery)
                     + "ms rows=" + generated.length)
        return generated
    }

    // RPM rows around a base (<= 0 seeds from the neutral anchor, unhighlighted).
    // Pure integers; RPM stays > 0 — a motor has no negative speed, and a 0 row
    // would commit as the explicit-clear sentinel.
    function rpmRowsFor(base) {
        var rpmSet = base > 0
        var anchor = rpmSet ? base : root.rpmDefaultAnchor
        // Once per snapshot, not once per row — this runs a query.
        var _t0 = Date.now()  // TEMPORARY — see grindRowsFor
        var rpmStepValue = root.rpmStep()
        var _tQuery = Date.now()
        var out = []
        var seen = ({})
        for (var n = -root.rpmWindowSteps; n <= root.rpmWindowSteps; n++) {
            var rpm = anchor + n * rpmStepValue
            if (rpm <= 0) continue
            var v = String(rpm)
            if (seen[v]) continue
            seen[v] = true
            out.push({ value: v, isCurrent: rpmSet && n === 0 })
        }
        console.info("[Equipment] grind picker: rpmRowsFor step-query="
                     + (_tQuery - _t0) + "ms generate=" + (Date.now() - _tQuery)
                     + "ms rows=" + out.length)
        return out
    }

    // NOTE: this component exposes no reactive rows property on purpose. An
    // earlier design had `grindRows`/`rpmRows` bindings; the picker must not
    // consume rows reactively, because a rebuild under an open Tumbler resets
    // the view mid-interaction (see GrindPickerDialog's snapshot rationale).
    // Callers take an explicit snapshot via grindRowsFor()/rpmRowsFor() and
    // rebuild at defined moments. The steps are derived inside those calls from
    // the live database, so a snapshot is current when it is taken and there is
    // nothing to listen for.
}
