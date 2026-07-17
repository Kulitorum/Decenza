## 1. Data model & migration — coffee_bags

- [x] 1.1 Add `storageHint` (QString) and `openedDate` (QString, ISO date) fields to `CoffeeBag` in `src/history/coffeebagstorage.h`
- [x] 1.2 Add the next sequential migration in `src/history/shothistorystorage.cpp` adding nullable `storage_hint` TEXT and `opened_date` TEXT columns to `coffee_bags`; bump `kCols`/column-list constants used by `updateBagFieldsStatic`/`bagFromQueryRow`
- [x] 1.3 Update `CoffeeBag::toVariantMap()`/`fromVariantMap()` in `coffeebagstorage.cpp` to round-trip the two new fields
- [x] 1.4 Confirm `CoffeeBagStorage::importBagsStatic` (device transfer / backup restore) copies the two new `coffee_bags` columns via its existing generic column path; add explicit coverage if it uses an allowlist instead
- [x] 1.5 Confirm `touchesVisualizerFields()` correctly excludes `storageHint`/`openedDate` (local-only, same as `frozenDate`/`defrostDate`)

## 2. Data model & migration — shots (blocking: gap 3 cannot work without this)

`frozen_date`/`defrost_date` are dedicated `shots`-table columns, not just `coffee_bags` columns — the same migration/plumbing must be mirrored for `storage_hint`/`opened_date`, or the shot-snapshot requirements in `coffee-bag-model`/`bag-freeze-lifecycle` are unsatisfiable and gap 3's per-shot lifecycle data has nothing to read from on historical shots.

- [x] 2.1 In the same migration as task 1.2 (or a paired one), `ALTER TABLE shots ADD COLUMN storage_hint TEXT` and `ALTER TABLE shots ADD COLUMN opened_date TEXT`, mirroring `shothistorystorage.cpp:1038-1041`'s `frozen_date`/`defrost_date` pattern
- [x] 2.2 Add `storage_hint`/`opened_date` to the shot-save `INSERT` column list and bound parameters (mirror `shothistorystorage.cpp:1896`/`1910`/`1957-1958`)
- [x] 2.3 Add `storage_hint`/`opened_date` to the shot-read `SELECT` column list and the `ShotRecord`/metadata field mapping (mirror `shothistorystorage.cpp:2482`/`2542-2543`, and the `data.frozenDate = metadata.frozenDate` style assignment around `1719-1720`)
- [x] 2.4 Add `storage_hint`/`opened_date` to the device-transfer/backup-restore `INSERT` column list, including the source-column-presence index resolution used for older source databases that predate the columns (mirror `shothistorystorage.cpp:3371-3372` `idxFrozenDate`/`idxDefrostDate` and the `srcValueOrNull(idx)` binding at `3412`)
- [x] 2.5 Add `storageHint`/`openedDate` fields to `ShotProjection` (`src/history/shotprojection.h/.cpp`) and `src/history/shothistory_types.h`, following the existing `frozenDate`/`defrostDate` pattern
- [x] 2.6 Snapshot the active bag's `storageHint`/`openedDate` onto the shot record at save time (same call site that stamps `frozenDate`/`defrostDate` today)
- [x] 2.7 Add `storageHint`/`openedDate` to `CurrentBeanBlockInputs` and `beanInputsFromProjection()` in `src/ai/dialing_blocks.h`
- [x] 2.8 Add a migration test asserting an existing database gains both `shots` columns (NULL on existing rows) and a device-transfer test asserting a source DB predating the migration imports cleanly with both columns NULL (no per-row "unknown field" warning)

## 4. AI reasoning — beanFreshness block

- [x] 4.1 Extend `DialingHelpers::buildBeanFreshness()` (`src/ai/dialing_helpers.h`) to accept `storageHint`/`openedDate`, compute `freshnessKnown` from `frozenDate || defrostDate || openedDate`, and surface `storageHint`/`openedDate` in the JSON block when set
- [x] 4.2 Rewrite `kBeanFreshnessKnownInstruction` to add the reverse-direction guidance: a recent `defrostDate`/`openedDate` may mean the portion is under-rested/gassy (chokes, runs long, over-extracts, may want a coarser grind that settles over the following days), not merely "fresher"
- [x] 4.3 Update the aging-anchor logic to use whichever of `defrostDate`/`openedDate` is most recent when both are present
- [x] 4.4 Update `src/ai/shotsummarizer.cpp`'s `currentBean.beanFreshness` guidance (around lines 1003-1012) and the "Your beans are old/stale" forbidden-simplification note (around lines 1605-1613) to reflect the two-direction reasoning and the `openedDate`/`storageHint` fields
- [x] 4.5 Add/update unit tests for `buildBeanFreshness()` covering: frozen+defrost known, never-frozen+opened known, neither set (unknown), both defrost and opened set (most-recent-wins anchor)

## 5. AI reasoning — cross-portion lifecycle visibility

- [x] 5.1 Extend the `dialInSessions[].context`/per-shot hoisting in `src/ai/dialing_blocks.cpp` to include `frozenDate`/`defrostDate`/`storageHint`/`openedDate` alongside the existing identity fields (only emit per-shot when it differs from the session/resolved value) — this is a MODIFIED requirement on the existing hoisting logic, not new machinery
- [x] 5.2 Extend `buildBestRecentShotBlock()` to carry the candidate shot's lifecycle fields directly (no hoisting — it's a single object, not a session list) for comparison against the resolved shot's `currentBean.beanFreshness`
- [x] 5.3 No new system-prompt instruction for this (see design.md decision) — data exposure is the whole fix. Do NOT add discounting guidance here
- [x] 5.4 Add tests asserting the data is visible: a `bestRecentShot` candidate with an older `defrostDate` than the resolved shot shows both dates distinctly in the payload; a `dialInSessions` session spanning a thaw event correctly hoists the shared value and overrides on the differing shot
- [ ] 5.5 Before shipping, replay a synthetic lifecycle-mismatch payload (shaped like the #1032 follow-up's shot-937 session) through the actual system prompt in a one-off eval to see whether the model's response flags the mismatch without any new instruction — this is a cheap sanity check before committing to "no instruction needed," not a substitute for the post-ship spot-check below
- [ ] 5.6 After shipping, spot-check a few real dial-in transcripts for a lifecycle-mismatch case to see whether the AI actually acts on the exposed dates without further prompting — only add instruction text later if this check (or 5.5) shows it's needed

## 6. MCP surface

- [x] 6.1 Update the `bag_update` MCP tool schema/description in `src/mcp/mcptools_write.cpp` to document `storageHint`/`openedDate`
- [x] 6.2 Verify `bag_create`/`bag_update` MCP handlers accept and persist the two new fields

## 7. UI — Change Beans dialog & bag cards

- [x] 7.1 Add a `storageHint` dropdown (Counter / Airtight container / Vacuum-sealed / Fridge — no "Frozen" value) to the bag form in `qml/components/ChangeBeansDialog.qml`, visible only while the freeze toggle is OFF; clear `storageHint` to null when the freeze toggle is turned on
- [x] 7.2 Add an `openedDate` `BeanDateField` to the form (edit-mode only, mirroring the existing `defrostDateField` at `ChangeBeansDialog.qml:1667-1676` — "only directly editable in edit mode, the quick action on the bag card is the everyday path")
- [x] 7.3 Add a "Mark Opened" action to `qml/components/BagCard.qml` for non-frozen bags, mirroring the existing "Thaw" `AccessibleButton` (`BagCard.qml:415-424`), with its own `DatePickerDialog` instance (`openedDatePicker`)
- [x] 7.4 Change the existing "Thaw" button's `onClicked` (`BagCard.qml:423`, currently `thawDatePicker.openWithDate(card.defrostDate)`) to `thawDatePicker.openWithDate("")` — always default the picker to today instead of the bag's existing `defrostDate`. Wire the new "Mark Opened" button's `onClicked` to `openedDatePicker.openWithDate("")` the same way (never pre-filled with the existing `openedDate`)
- [x] 7.5 Update `BagCard.qml`'s `_metaParts` (`BagCard.qml:161-175`) to render the absolute date alongside the day count — "Thawed {date} ({N}d)" / "Opened {date} ({N}d)" — reusing the existing `formatRoastDate()`-style locale formatting; update `qml/components/BeanSummary.qml`'s equivalent freeze/open line the same way

## 8. Manual & issue cleanup

- [x] 8.1 Update the GitHub wiki manual (bean/bag section) to document the storage-hint dropdown and "Mark Opened" action
- [ ] 8.2 Close GitHub issue #1032, referencing this change as covering its remaining gaps (storage-hint field, under-rested/gassy direction guidance, cross-portion lifecycle visibility)

## 9. Verification

- [x] 9.1 Run the full C++ test suite (`tst_mcptools_dialing`, `tst_coffeebagstorage` or equivalent, any `shotsummarizer`/`dialing_helpers` unit tests) and fix regressions
- [x] 9.2 Manually verify in Qt Creator: create a non-frozen bag with a storage hint and opened date, pull a shot, confirm `dialing_get_context` (via MCP) reports `freshnessKnown: true` with the correct instruction text
- [ ] 9.3 Manually verify the "Mark Opened" action and storage-hint dropdown (including its hide-when-frozen behavior) render correctly and persist across app restart
