## 1. Header title change

- [x] 1.1 In `qml/pages/ShotDetailPage.qml`, update the header title computation (~lines 223-241) to append `" · " + shotData.grinderSetting` after the `(temp)` text when `shotData.grinderSetting` is non-empty, and additionally append `" · " + rpm + " rpm"` (reusing the `equipment.card.lastRpm` translation key already used by the metrics-row Grind cell) when `shotData.rpm > 0`.
- [x] 1.2 Verify the header is unchanged for shots with no recorded grind setting (empty `grinderSetting`).
- [x] 1.3 Apply the same header change to `qml/pages/PostShotReviewPage.qml` (~lines 864-881), binding to the page's live edit state `editGrinderSetting`/`editRpm` (not `editShotData.grinderSetting`/`rpm`), so the header reflects an in-progress edit.
- [x] 1.4 Wrap the appended `(temp) · grind · rpm` portion in `<span style="font-size:<Theme.labelFont.pixelSize>px">...</span>` on both pages so it renders at the same size as the adjacent date text and fits without overlapping the date/badges.

## 2. Verification

- [ ] 2.1 Manually check the Shot Detail and Shot Review pages for: a shot with grind + RPM, a shot with grind only, and a shot with no grind — confirm header text, that the suffix renders at the same size as the date text, that it no longer overlaps the date/badges, and that long grind strings still elide correctly instead of breaking layout.
- [x] 2.2 Confirm both header changes are scoped to their title-text bindings only, no other page logic touched.
