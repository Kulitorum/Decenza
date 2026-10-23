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
      `calib_coarse_shot_id` (`REFERENCES shots(id) ON DELETE SET NULL`); bump
      `kCurrentSchemaVersion`
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
- [ ] 2.5 Run `tools/calib_analysis.py` against the markpalmos database (wide-span
      linearity check) and record the result here

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

## 4. ShotServer

- [x] 4.1 `GET /api/equipment/<id>/calibration`, `POST /api/equipment/<id>/calibration`
      (set/clear a side)
- [x] 4.2 `/equipment` cards show the section; web shot page offers the marks

## 5. Tests

- [x] 5.1 `tst_dialing_blocks`: anchors-only key, combination median, exclusion from
      mined pairs, range widening, agreement/disagreement, read-time invalidation,
      each refusal code, no-anchor payload unchanged apart from `basis`
- [x] 5.2 Equipment storage: FK clear on shot delete, merge fill, fork not carried,
      device-transfer remap; migration 46

## 6. Docs

- [x] 6.1 `dialing_get_grinder_calibration.md` and `profile_knowledge.json`: `basis` and
      `markedAnchors`, briefly
- [ ] 6.2 Wiki manual: short Grind calibration entry (Equipment page + marking a shot)
- [x] 6.3 Logging per `docs/CLAUDE_MD/LOGGING.md` for mark/clear and disagreement
