## Context

`ShotDetailPage.qml` builds its header title as a single JS-computed string (`qml/pages/ShotDetailPage.qml:223-236`):

```js
var name = shotData.profileName || TranslationManager.translate("shotdetail.title", "Shot Detail")
var t = shotData.temperatureOverrideC
if (t !== undefined && t !== null && t > 0) {
    result = name + " (" + Math.round(Theme.cToDisplay(t)) + Theme.tempUnitSuffix() + ")"
} else {
    result = name
}
return Theme.replaceEmojiWithImg(result, Theme.titleFont.pixelSize)
```

The metrics row further down the same page already displays grind/RPM as a separate cell (`shot-detail-metrics` capability, `ShotDetailPage.qml:646-687`), using the convention `<grind>` + `" · " + "<rpm> rpm"` when RPM is non-zero, and hiding entirely when `grinderSetting` is empty. This is a single, low-complexity string-formatting change confined to one QML binding; no new architecture, dependency, or data-model change is involved.

`PostShotReviewPage.qml` has an identical header pattern (not a shared component — the logic is duplicated), built from `editShotData.profileName`/`temperatureOverrideC` (`qml/pages/PostShotReviewPage.qml:864-876`). That page is a form: grind and RPM are edited live via `editGrinderSetting`/`editRpm` properties (initialized from `editShotData.grinderSetting`/`rpm` at load, `PostShotReviewPage.qml:182-183`), and the page's own dirty-check (`hasUnsavedChanges`, lines 378-379) already treats `editGrinderSetting`/`editRpm` as the current values and `editShotData.grinderSetting`/`rpm` as the last-saved baseline.

## Goals / Non-Goals

**Goals:**
- Append grind setting (and RPM, when known) to both the Shot Detail and Shot Review header titles, reusing the existing grind-format convention so it reads consistently with the metrics-row cell and Shot Plan.
- Keep today's header unchanged for shots with no recorded grind setting.
- Keep the header's accessible name in sync with its visible text.
- On the Shot Review page, reflect an in-progress grind/RPM edit in the header immediately (bind to `editGrinderSetting`/`editRpm`, not the last-saved `editShotData` values).

**Non-Goals:**
- Changing the metrics-row Grind cell or Equipment card.
- Any change to how `grinderSetting`/`rpm` are captured or stored.
- Extracting the two pages' duplicated header logic into a shared component — out of scope for this change; each page keeps its existing (duplicated) header binding, just with the grind/RPM suffix added to each.

## Decisions

- **Format**: append `" · " + grinderSetting` after the `(temp)` parenthetical, then `" · " + rpm + " rpm"` when `rpm > 0`. Example: `Default (90°C) · 5.5 · 340 rpm`. This mirrors the metrics-row cell's `<grind> · <rpm> rpm` pattern (`shot-detail-metrics` spec) rather than inventing a new separator style. Same format on both pages.
- **Visibility**: suffix is omitted entirely when the grind setting is empty (`shotData.grinderSetting` on Shot Detail, `editGrinderSetting` on Shot Review) — same gating the metrics-row cell already uses — so shots without a recorded grind keep today's exact header text.
- **Source of truth on Shot Review**: the header binds to `editGrinderSetting`/`editRpm`, not `editShotData.grinderSetting`/`rpm`. This page is a live edit form; every other consumer of grind/RPM state on it (the dirty-check, the save payload) already treats the `edit*` properties as current. Binding the header to the stale `editShotData` values would make it disagree with the editable fields below it while the user is mid-edit.
- **Truncation**: no new elide/width handling is added beyond what each `Text` element already does (`elide: Text.ElideRight`, `Layout.fillWidth: true`) on both pages. Each header is a single line that already truncates gracefully on overflow; adding a bounded suffix (short free-text grind + short rpm number) does not require new layout logic. The Shot Review header row is more crowded (date + quality badges + sparkle + toggle), so it will elide sooner than Shot Detail's — accepted as a trade-off per the proposal.
- **Suffix font size**: the `" (temp)" + " · <grind>" + " · <rpm> rpm"` portion is wrapped in `<span style="font-size:<Theme.labelFont.pixelSize>px">...</span>` (both pages already run `textFormat: Text.RichText`), matching the exact pixel size of the sibling date `Text` (which uses `font: Theme.labelFont`) so the suffix reads at the same visual weight as the date rather than an arbitrary relative step down from the title. The profile name stays at full `Theme.titleFont` size; only the appended metadata shrinks, which also gives the row more horizontal room before eliding. `Theme.replaceEmojiWithImg` runs on the full string after the `<span>` tag is added — it only matches emoji codepoints, so it passes the tag through untouched.
- **Accessibility**: neither header `Text` has an explicit `Accessible.name`, so screen readers read the visible `text` content directly — the appended suffix is picked up automatically with no separate accessibility binding needed on either page.

## Risks / Trade-offs

- [Long free-text grind settings could crowd out the profile name on narrow/mobile widths] → Mitigated by the existing `Text.ElideRight` + `Layout.fillWidth` behavior already on both headers; no shot in practice has a grind string long enough to fully hide the profile name, and this matches how the metrics-row cell already handles long grind text (width-capped there; here it's part of a single eliding line).
- [Shot Review's header row is already the most crowded of the two — adding grind/RPM there increases the chance of early eliding on narrow (phone/tablet-portrait) widths] → Accepted per explicit user direction to add the suffix there too; no additional layout changes are in scope for this change, and the row already elides gracefully rather than overlapping other elements.
