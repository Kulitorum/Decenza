# Design: Grind Calibration Anchors on the Equipment Package

Phase 1 context (grind model `grind ≈ batchBaseline + UGS·conversionKey`, the
within-batch pair mining, its constants) is in the archived
`openspec/changes/archive/2026-07-07-fix-grinder-calibration-cross-profile/`.

## Context

Phase 1 already calibrates automatically and invisibly. The first #2070 draft added a
second, parallel calibration — a per-(model, burrs) QSettings record whose key
overrode the mined one — that skipped Phase 1's same-batch, dialed-in, badge, RPM and
basket rules. This design makes the deliberate pair a citizen of Phase 1 instead, and
puts the whole calibration where the user can see it.

## Decisions

### D1. Anchors are two nullable shot references on the package

`equipment_packages.calib_fine_shot_id` and `calib_coarse_shot_id`, added by
migration 46 with an `AFTER DELETE ON shots` trigger that clears a side whose shot is
deleted. Not a foreign key: with `foreign_keys` on, SQLite refuses every write to a
child table whose parent table is missing. The package is the scope
Phase 1 already mines (`AdviceScope`), so the anchors are scoped the same way and go
away with the package. Referencing shots — rather than copying setting/UGS numbers —
means a read always re-derives from the real shot, so there is nothing to go stale or
be hand-edited out of sync.

### D2. Lifecycle

| Event | Rule |
|---|---|
| Anchor shot deleted | Migration 46 trigger clears the side |
| Anchor shot moved to another package, or edited out of eligibility | Read-time check (D4) ignores it and the status says why; the stored id stays until replaced or cleared |
| Merge (`mergePackagesStatic`) | Target keeps its anchors; fills unset sides from the source |
| Copy-on-write fork | Not carried — Phase 1 history does not follow a fork either |
| Hard delete | Only possible with no shots, so there are no anchors |
| Device transfer | Post-pass after the shot import remaps package and shot ids; an existing destination package keeps its own anchors and fills unset sides |

### D3. One computation, three consumers

`buildGrinderCalibrationBlock` takes an optional `GrindAnchorCandidate` and fills an
optional `GrindCalibrationStatus`. The AI block, the equipment card/review-page status
(`grindCalibrationForPackage`) and the mark validation (`markGrindCalibrationAnchor`)
all run that one computation, so "same batch", "eligible" and "agrees" have one
definition each. Marking evaluates the package with the *candidate* anchor and
refuses unless that side is eligible and, when the other side is set, the pair is
valid.

Calibration only reads shots, by plain column reads. It never goes through
`loadShotRecordStatic`, which re-runs the badge detectors and persists any drift
(`docs/SHOT_REVIEW.md`, "Lazy persist on view").

### D4. Eligibility and pair rules

Per shot: espresso, in the package, ≥ 15 g, no quality badge, numeric grinder setting,
UGS-placed profile, bean identity present. A marked shot counts as dialed-in (it is
the user's deliberate pick), so it bypasses the rating/yield/refractometer test, and
one shot per side is enough. Pair: same batch (Phase 1 identity), same RPM,
|ΔUGS| ≥ 2.0. The per-shot SQL filter Phase 1 used moves into C++ so marking and
mining share the predicate.

### D5. Combination

Anchor shots are excluded from mined pairs, so the deliberate pair is not counted
twice. Publish when the Phase 1 gate passes OR the anchor pair is valid; with a valid
pair the key is the median over mined slopes plus the anchor slope, and the pair's
span joins the validated range like any pair. "Agrees with history" uses the Phase 1
tolerance (`maxSpreadRatio · |minedMedian|`, same sign). Disagreement is reported,
never silently enforced.

The deliberate pair widens the range exactly as a mined pair does — one rule for all
pairs. The first draft's default-off long-hop gate is dropped. Whether grind is
linear enough across a wide span is checked offline against the markpalmos database
with `tools/calib_analysis.py` before merge, not by a switch that ships off.

### D6. Payload

`confidence` keeps its Phase 1 values (`approximate` / `directional`); the never-built
`"calibrated"` value is removed from the spec. `basis` (`history` / `anchors` /
`both`) appears whenever a key publishes; `markedAnchors` appears only when the
package has an anchor set. Both surfaces (MCP and in-app advisor) still go through
`buildGrinderCalibrationBlock`.

### D7. UI

- **Equipment card** — full Grind calibration section, always visible: status line,
  profile range, one line per anchor (tap opens the shot, ✕ clears), plus one line for
  disagreement or an anchor that no longer counts. Copy says anchors are optional,
  and "not needed" when history suffices.
- **Post-shot review** — the same status line and Mark fine / Mark coarse, outside the
  equipment and recipe cards so recipe shots get it. Unsaved edits are saved first;
  the mark is sent on `shotMetadataUpdated` for that shot (event-driven, no timer).
- **ShotServer** — the same section on `/equipment` cards and the mark actions on the
  web shot page, through the same computation.

One QML component (`GrindCalibrationSection`) renders both app surfaces. The card
names the profile range suggestions cover but does not list the numbers: they
belong to a coffee, not to the equipment.

### D9. Where the numbers reach the user: the recipe wizard

The recipe wizard's grind step already hints from this bean's last grind, direction
only. `grindSuggestionFor` runs the calibration anchored on the selected bag's
latest shot on the selected equipment and returns the picked profile's entry when
it carries a number; the wizard shows it in the same callout with a **Use** button
and never fills the field itself (an estimate can be wrong — #1223). The web recipe
editor is left alone; it is being replaced.

### D8. The original UGS guide, live, in a dialog

The section links the chart author's own guide
(`https://videoblurb.com/UGS/ugsinfo.html`) rather than paraphrasing it — credit
where it is due, and following the guide (Cremina then Rao Allongé on one bag) is
exactly a valid anchor pair. The page is built with external CSS, web fonts and
JavaScript, so Qt rich text (the release-notes path) cannot reproduce it; it is
loaded live in Qt WebView, with its URL shown above it. Qt WebView renders with
the OS web view on Android, iOS and macOS and WebView2 on Windows
(`qtwebview-index.html`, Qt 6.12). On Linux it needs Qt WebEngine (Chromium), so
Linux opens the same URL in the system browser instead and does not link Qt
WebView.

## Non-Goals

- An MCP action for marking anchors.
- Modelling a nonlinear UGS↔setting curve.
- Cross-user calibration.
