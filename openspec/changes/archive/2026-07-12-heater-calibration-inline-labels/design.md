# Design: Heater Calibration Inline Labels

## Context

The Heater Calibration popup lives at the bottom of [SettingsCalibrationTab.qml](qml/pages/settings/SettingsCalibrationTab.qml) (`calibrationPopup`, ~lines 720–836). Its content is a `Flickable`-wrapped `ColumnLayout` with `spacing: Theme.scaled(16)` containing: title, separator, then five pairs of (caption `Text` label above a full-width `ValueInput`), a separator, the "Defaults for cafe" button, and a Cancel/Done row. The dialog height is capped at `parent.height * 0.85`, and the stacked label-over-value pattern makes the column tall enough that the Done button falls below the fold on typical screens.

`ValueInput` auto-sizes its `implicitWidth` from its display text (via `TextMetrics`), so it does not need `Layout.fillWidth` to render correctly — the current full-width stretch is purely a layout convenience.

## Goals / Non-Goals

**Goals:**
- Halve the vertical footprint of the five parameter rows by placing each label to the left of its `ValueInput`.
- Done/Cancel visible without scrolling at typical tablet and desktop window sizes.
- Zero behavioral change: same settings, ranges, steps, display texts, tab order, accessible names.

**Non-Goals:**
- No changes to `ValueInput` itself.
- No changes to other dialogs or the calibration card in the tab body.
- No new translation keys or settings.

## Decisions

1. **`RowLayout` per parameter, label left + `ValueInput` right.**
   Each existing `Text` + `ValueInput` pair becomes:
   ```qml
   RowLayout {
       Layout.fillWidth: true
       spacing: Theme.scaled(12)
       Text {
           Layout.fillWidth: true
           text: ...same key/fallback...
           font: Theme.captionFont
           color: ...same color...
           wrapMode: Text.WordWrap
       }
       ValueInput { id: ...same id...; /* no Layout.fillWidth */ ... }
   }
   ```
   The label takes the remaining width (`Layout.fillWidth: true`) and wraps if a translation is long; the `ValueInput` sits right-aligned at its implicit width. Alternative considered: a two-column `GridLayout` for the whole block — rejected because the per-row `RowLayout` is a smaller diff, keeps each parameter self-contained, and GridLayout column sizing would couple all rows to the widest label.

2. **Drop `Layout.fillWidth` on the `ValueInput`s and rely on implicit width.**
   `ValueInput` computes `implicitWidth` from its text metrics plus button chrome, so right-aligning at natural size keeps the +/- buttons adjacent to the value (shorter drag/tap travel) and gives the label the leftover space. Alternative: keep a fixed `Layout.preferredWidth` — rejected; implicit width already adapts to the longest display text (e.g. "Always on").

3. **Keep the `Flickable` wrapper.**
   The dialog should no longer need to scroll, but tiny phone-landscape windows may still clip; the Flickable stays as a harmless safety net. `height` binding on the Dialog is unchanged — it already tracks `calibrationColumn.implicitHeight`, so the dialog simply gets shorter.

4. **Leave title, separators, defaults button, and Cancel/Done row untouched.**
   Only the five label+input pairs are restructured. Tab order (`KeyNavigation` chain) is id-based and ids are unchanged, so no edits needed there.

5. **Tank preheat fix: write per profile upload, not per connect (bundled bug fix).**
   `uploadProfile()` and `uploadProfileAndStartEspresso()` in `src/ble/de1device.cpp` gain one line each (or a shared helper): `writeMMR(DE1::MMR::TANK_TEMP_THRESHOLD, std::clamp(qRound(profile.tankDesiredWaterTemperature()), 0, 45))`. Rationale:
   - Per-upload matches both reference implementations (de1app writes it in `de1_send_shot_frames`; ReaPrime in its profile setter) and means the next profile naturally overrides/clears the previous one's preheat — no restore bookkeeping.
   - The `m_lastMMRValues` dedup cache makes repeat uploads free; `writeMMR` also brings the firmware-flash guard.
   - The connect-time `TANK_TEMP_THRESHOLD = 0` in `sendInitialSettings()` stays: it is the safe baseline before any profile lands, and the real profile upload follows ~500 ms later via `applyAllSettings()`.
   - **Deliberately skipped**: de1app's "write 60 °C for ~4 s to force circulation, then the real value" dance for targets ≥10 °C. It is a timer-based workaround (against our no-timers convention), ReaPrime skips it too, and the firmware converges on the threshold without it.
   - **Deliberately skipped**: de1app's advanced-profiles-only gate (`settings_2c`). That gate exists because de1app's UI only edits the field for advanced profiles; in Decenza the value is per-profile JSON data, and non-advanced profiles default to 0 anyway.

## Risks / Trade-offs

- [Long translations (e.g. German) could squeeze the label] → `wrapMode: Text.WordWrap` + `Layout.fillWidth` lets the label wrap to two lines within its row; the row grows only when actually needed instead of always costing a full row.
- [Losing `Layout.fillWidth` on `ValueInput` narrows the drag surface for the drag-to-adjust gesture] → the full-screen scrubber popup (tap value) remains the primary precise-adjust path; the compact control keeps its +/- buttons and drag at the same physical size used elsewhere in the app (e.g. status strips) where `ValueInput` already renders at implicit width.
- [Tank preheat becoming active is a behavior change for profiles that carry the field] → this is the documented intent of the field (de1app shows "Preheat water tank at N ºC" for such profiles); clamping to 45 °C matches de1app's range check and bounds any malformed imported profile.

## Migration Plan

QML edit in one file plus a two-call-site C++ change; no data, settings, or API migration. Rollback = revert commit.

## Open Questions

None.
