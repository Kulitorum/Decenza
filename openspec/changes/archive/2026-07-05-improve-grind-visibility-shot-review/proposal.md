# Improve Grind Visibility on Shot Review Surfaces

## Why

Grind setting is the single most consulted value when reviewing a past shot to decide the next move, yet on the shot detail page it is buried in caption-sized text inside the Equipment card, two scrolls below the graph ([#1413](https://github.com/Kulitorum/Decenza/issues/1413), requests 1 & 2). Request 1 (RPM in the Shot Plan) already shipped in [#1396](https://github.com/Kulitorum/Decenza/pull/1396); this change addresses the remaining presentation gaps. On the post-shot review page, the equipment identity card also currently sits *above* the editable per-shot fields, pushing the values users actually edit further down the page.

## What Changes

- **Shot detail page**: add a **Grind** cell to the top metrics row (Duration · Dose · Output · Ratio · Rating), positioned after Dose. Shows the shot's grind setting in the standard metric style, with RPM as a caption suffix (`5.5 · 340 rpm`) when recorded — matching the Shot Plan's format. The cell is hidden entirely when the shot has no recorded grind setting (row reflows to five metrics). Long free-text settings are width-capped and elided; the Equipment card keeps its existing un-elided grind/RPM line (intentional duplication).
- **Post-shot review page**: move the equipment identity card (grinder + basket + puck prep, with its Change Equipment button) from the top of the 3-column field grid to the very bottom — after Grind setting, RPM, Barista, Preset, and Shot date. Editable per-shot values come first; hardware context last. Pure reorder, no behavior change.
- **Wiki manual**: touch up any Manual.md description of the shot detail metrics row or post-shot review field order affected by these moves.

## Capabilities

### New Capabilities
- `shot-detail-metrics`: what the shot detail page's top metrics row displays, including the new Grind cell (content, position, RPM suffix, empty-state hiding, truncation).
- `post-shot-review-layout`: ordering of the post-shot review page's field grid (editable fields first, read-only metadata next, equipment identity card last).

### Modified Capabilities

_None — no existing spec covers these surfaces, and no other capability's requirements change._

## Impact

- `qml/pages/ShotDetailPage.qml` — new metrics-row cell (presentation only; `shotData.grinderSetting` / `shotData.rpm` already loaded).
- `qml/pages/PostShotReviewPage.qml` — child reorder within the existing `GridLayout`; update the stale "edited in the fields just below" comment on the equipment card.
- Decenza.wiki `Manual.md` — minor wording updates (separate wiki repo, cloned at `~/Development/GitHub/Decenza.wiki`).
- New translation key for the metric label (e.g. `shotdetail.grind`); RPM suffix reuses the existing format.
- No schema/storage changes, no new settings, no C++ changes, no history-list changes (rows already show grind). Issue #1413 request 3 (dialed-for-profile stamp) is explicitly out of scope.
