## Why

Auditing issue [#1032](https://github.com/Kulitorum/Decenza/issues/1032) against the shipped bean-bag-inventory work (#1327/#1370) found the headline complaint (`freshnessKnown` hardcoded `false`) already fixed, but three real gaps remain. First, `freshnessKnown` only flips `true` via `frozenDate`/`defrostDate` — a bag that is never frozen (likely most users) always reports storage as unknown, so the AI is instructed to ask about storage on every single turn even when "opened 3 days ago, kept in an airtight container" would be a sufficient answer. Second, the shipped `beanFreshness` instruction only teaches "don't assume calendar-age staleness, count from the thaw date" — it never teaches the reverse: a recently-thawed or recently-opened portion is often **under-rested/gassy** (chokes, runs long, over-extracts, wants a **coarser** grind for the first few days), which is the exact failure mode reproduced live in the issue's follow-up comment (shot 937). Third, `dialInSessions`/`bestRecentShot` carry no freeze/defrost/storage state per historical shot — only the single resolved "current" shot gets a `beanFreshness` block — so the AI has no way to notice that a best-rated anchor shot came from a different, longer-rested portion of the same bag and nearly anchored a grind recommendation on it in that same session. This third gap turns out to need only data exposure, not a new instruction: once each shot's own lifecycle dates are visible, that fits the existing "per-shot field overriding session context signals something changed" pattern the payload already uses for `grinderBrand`/`beanBrand` — no bespoke "discount this" guidance required.

## What Changes

- Add `storageHint` (enum: `counter` / `airtight` / `vacuum-sealed` / `fridge` — non-frozen storage only, no "frozen" value; frozen state stays solely defined by `frozenDate` being set, as it already is today) and `openedDate` (nullable date) to `CoffeeBag` — the non-frozen analogue of `frozenDate`/`defrostDate`, giving every bag a "this portion started aging" anchor whether or not it was ever frozen.
- Add a "Mark Opened" action on non-frozen bag cards (mirrors the existing "Thaw" action) and a storage-hint dropdown + opened-date picker in the Change Beans bag form, shown only while the freeze toggle is off.
- Snapshot the same two fields onto the `shots` table (mirroring the existing `frozen_date`/`defrost_date` columns) — the save-time INSERT, the read-back SELECT, and the device-transfer/backup-restore copy path all need the two new columns, not just the `coffee_bags` side.
- Change "Thaw" (and the new "Mark Opened") to always default its calendar picker to today's date rather than to the bag's existing stored date — today is by far the most probable answer for a new thaw/open event, so the picker should default there instead of re-showing a stale previous date.
- Display the actual thaw date (not just "Def {N}d") on the bag inventory card when `defrostDate` is set, mirroring the existing `roastDate` display convention (absolute date is more meaningful than a relative count); same treatment for `openedDate`.
- Extend `buildBeanFreshness()`: `freshnessKnown` becomes `true` when `openedDate` is set (not just frozen/defrost dates); `storageHint` is surfaced in the JSON block when set.
- Add AI instruction text covering the reverse-direction case: a recent thaw/open date can mean the coffee is under-rested/gassy, not stale, and may want a coarser grind that settles back over the following days.
- Snapshot `storageHint`/`openedDate` onto each shot at save time (same pattern as `frozenDate`/`defrostDate`), and surface each shot's own lifecycle dates in `dialInSessions`/`bestRecentShot` — no new instruction text needed; the existing per-shot-override pattern already teaches the AI to treat a differing field as meaningful.

## Capabilities

### New Capabilities
(none — this extends existing bag and dialing capabilities rather than introducing a new domain concept)

### Modified Capabilities
- `coffee-bag-model`: add `storageHint` and `openedDate` fields to the `CoffeeBag` struct and `coffee_bags` table (migration), plus backup/device-transfer copy support.
- `bag-freeze-lifecycle`: extend the current-portion lifecycle concept to the non-frozen case (`openedDate`, "Mark Opened" action, storage-hint dropdown shown only when not frozen), alongside the existing freeze/thaw behavior. (The bag form lives in the Change Beans dialog, but its freeze/lifecycle fields are already specified under this capability, not `change-beans-dialog`.)
- `dialing-context-payload`: extend `buildBeanFreshness`'s `freshnessKnown`/`storageHint` logic, add the under-rested/gassy direction guidance to the `beanFreshness.instruction` text and the `shotsummarizer.cpp` system prompt, and add each shot's own lifecycle snapshot data (no new instruction) to `dialInSessions`/`bestRecentShot` so cross-portion history is distinguishable.

## Impact

- `src/history/coffeebagstorage.h/.cpp` — `CoffeeBag` struct, storage helpers, backup/device-transfer copy
- `src/history/shothistorystorage.cpp` — new migration for `coffee_bags` AND `shots` columns; shot-save `INSERT`, shot-read `SELECT`/`ShotRecord` mapping, and the device-transfer `INSERT` + source-column-index resolution for `shots`
- `src/history/shotprojection.h/.cpp`, `src/history/shothistory_types.h` — per-shot snapshot fields
- `src/ai/dialing_helpers.h` — `buildBeanFreshness()`, instruction strings
- `src/ai/dialing_blocks.h/.cpp` — `CurrentBeanBlockInputs`, `shotToJson`, `buildDialInSessionsBlock`, `buildBestRecentShotBlock`
- `src/ai/shotsummarizer.cpp` — system-prompt guidance text
- `src/mcp/mcptools_write.cpp` — `bag_update` MCP tool schema
- `qml/components/ChangeBeansDialog.qml` (bag creation/edit form), `BagCard.qml`, `BeanSummary.qml` — storage-hint/opened-date UI and display
- GitHub issue #1032 — closed as superseded, with a reference to this change covering the remaining gaps it identified
