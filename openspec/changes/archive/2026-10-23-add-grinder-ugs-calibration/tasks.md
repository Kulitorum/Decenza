# Tasks: Grind Calibration Anchors on the Equipment Package

## 0. Unwind the first draft

- [x] 0.1 Remove `SettingsGrinder` (sources, `Settings` wiring, `settings_qml.h`, CMake
      lists) and the precedence override / long-hop gate in `dialing_blocks.cpp`
- [x] 0.2 Revert the snapshot threading (`aimanager.cpp`, `mcptools_ai.cpp`,
      `mcptools_dialing.cpp`, `registerAITools` signature + test stubs) and the
      `"calibrated"` prose in `profile_knowledge.json` / `dialing_get_grinder_calibration.md`
- [x] 0.3 Merge current `main` into the branch

## 1. Storage

- [x] 1.1 Migration 46: `equipment_packages.calib_fine_shot_id` /
      `calib_coarse_shot_id` plus a `shots` AFTER DELETE trigger that clears a side;
      bump `kCurrentSchemaVersion`
- [x] 1.2 `EquipmentStorage` read/write of anchors; merge fills empty target sides;
      fork does not copy them
- [x] 1.3 Device-transfer post-pass remapping package + shot ids

## 2. Calibration computation

- [x] 2.1 `buildGrinderCalibrationBlock` takes an optional anchor candidate and fills an
      optional status; move the per-shot SQL filter into a shared C++ predicate
- [x] 2.2 Anchor pair: eligibility, pair rules, exclusion from mined pairs, publish
      rule, median, range, agreement
- [x] 2.3 Payload: `basis`, `markedAnchors`; drop `"calibrated"`
- [x] 2.4 Mark validation through the same computation; refusal codes
- [x] 2.5 Run `tools/calib_analysis.py` against the markpalmos database (wide-span
      linearity check) and record the result here.
      Result (#1223 attachment, 5434 shots, not committed): within-coffee pair slopes
      bucketed by UGS span, `dialed` eligibility. Wide spans are not less linear than
      narrow ones; they are the cleaner signal. DF83V: span <2 median 0.00 (12+/15-),
      span 4+ median 0.86 (23+/2-). Mignon Specialita: medians 0.12 / 0.10 / 0.10 for
      spans <2 / 2-4 / 4+. Neither grinder passes the Phase 1 spread gate overall
      (directional), unchanged. No evidence against one rule for anchor and mined pairs.

## 3. App UI

- [x] 3.1 `AIManager`: async package calibration request + mark/clear, with change
      notification
- [x] 3.2 `GrindCalibrationSection.qml` (status, range, anchors with open/clear,
      optional wording)
- [x] 3.3 Equipment card shows the section
- [x] 3.4 Post-shot review: status + mark actions outside the equipment/recipe cards;
      save-then-mark
- [x] 3.5 UGS guide dialog: Qt WebView (`find_package(Qt6 WebView)`,
      `QtWebView::initialize()` before the app object), live page with its URL
      shown above it and an open-in-browser action; add `qtwebview` to the
      Android, iOS, macOS and Windows CI workflows. Linux opens the URL in the
      system browser and does not link Qt WebView

- [x] 3.6 Recipe wizard: calibrated starting-grind suggestion with a Use button
      (`grindSuggestionFor`, `AIManager::requestGrindSuggestion`); card range line
      reworded to "Grind suggestions from … to …"

## 4. ShotServer

- [x] 4.1 `GET /api/equipment/<id>/calibration`, `POST /api/equipment/<id>/calibration`
      (set/clear a side)
- [x] 4.2 `/equipment` cards show the section; web shot page offers the marks

## 5. Tests

- [x] 5.3 `tst_dialing_blocks`: wizard suggestion (estimate inside the range, none
      outside it, none for a bag with no shot on the equipment)
- [x] 5.1 `tst_dialing_blocks`: anchors-only key, combination median, exclusion from
      mined pairs, range widening, agreement/disagreement, read-time invalidation,
      each refusal code, no-anchor payload unchanged apart from `basis`
- [x] 5.2 Equipment storage: merge fill, fork not carried, device-transfer remap;
      migration 46's delete trigger (in `tst_dialing_blocks`, which runs migrations)

## 6. Docs

- [x] 6.1 `dialing_get_grinder_calibration.md`: `basis` and `markedAnchors`, briefly
      (the advisor prompt in `profile_knowledge.json` is unchanged)
- [x] 6.2 Wiki manual: "Grind Calibration (UGS)" section in chapter 5 plus a glossary
      entry, linking the author's guide; push to the wiki when this PR merges
- [x] 6.3 Logging per `docs/CLAUDE_MD/LOGGING.md` for mark/clear and disagreement

## 7. Equipment archive (found while verifying: package 26 hidden but in use)

- [x] 7.1 Storage: archived list (removed, not forked), restore (refused only for a
      fork; no name check, renames keep that rule), create and identity edit revive a
      matching archived package
- [x] 7.2 App: "Remove" becomes **Archive**; "Show archived (N)" with Restore cards
- [x] 7.3 Web: `GET /api/equipment` returns archived; `POST /api/equipment/<id>/restore`;
      `/equipment` gets Archive, Show archived and Restore
- [x] 7.4 Tests: archived list excludes forks; create and edit revive; restore refuses
      forks and ignores a shared name
- [x] 7.5 Manual: Equipment Packages paragraph (Archive, Show archived, Restore);
      pushed with 6.2 when this PR merges

