## Why

The Shot Detail page header currently shows only `<Profile Name> (<Temp>°C)` — e.g. "Default (90°C)". Grind setting and RPM are already recorded per-shot (`shotData.grinderSetting`, `shotData.rpm`) and already surface in the metrics row and Equipment card further down the page, but a user scanning the header alone has no dial-in context. Surfacing grind (and RPM, when known) right in the header lets the user identify how a shot was ground without scrolling to the metrics row. The Shot Review page (`PostShotReviewPage.qml`) has the same header pattern and the same gap, so the same change applies there too.

## What Changes

- **MODIFIED** `ShotDetailPage.qml` header title — after the existing `<Profile> (<Temp>°C)` text, append the grind setting, and the RPM when it's non-zero, using the app's established grind-format convention (`· <grind>` and `· <rpm> rpm`, matching the metrics-row Grind cell and Shot Plan format).
  - When the shot has no recorded grind setting, the header is unchanged from today (`<Profile> (<Temp>°C)`).
  - When grind is set but RPM is 0/unset, only the grind suffix is appended.
  - The header's accessible name (read by screen readers) includes the grind/RPM suffix so TalkBack/VoiceOver users get the same information sighted users see.
- **MODIFIED** `PostShotReviewPage.qml` header title — same append, bound to the page's live edit state (`editGrinderSetting`/`editRpm`) rather than the original `editShotData.grinderSetting`/`rpm`, so the header reflects an in-progress grind/RPM edit the same way the rest of the page already treats those fields as the live source of truth (see `hasUnsavedChanges` dirty-check).
- **NOT in scope**: changing the existing metrics-row Grind cell or Equipment card — those already display grind/RPM and are unaffected.

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
- `shot-detail-metrics`: adds a requirement that both the Shot Detail page header and the Shot Review page header (title line) also display grind setting and RPM, alongside the existing metrics-row and Equipment-card requirements.

## Impact

- Affected code: `qml/pages/ShotDetailPage.qml` and `qml/pages/PostShotReviewPage.qml` (header title bindings only).
- No data-model changes — `grinderSetting`/`rpm` (and `editGrinderSetting`/`editRpm`) are already populated.
