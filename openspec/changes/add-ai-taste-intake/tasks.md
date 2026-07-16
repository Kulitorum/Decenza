# Tasks — Tap-only taste intake

## 1. Storage (shots migration 33)

- [x] 1.1 Add shots **migration 33** in `shothistorystorage.cpp`: `ALTER TABLE shots ADD COLUMN taste_balance TEXT` and `taste_body TEXT`; bump schema to 33. (empty = unset; no DEFAULT needed — pre-33 rows read NULL/"".)
- [x] 1.2 Wire the two columns through select projection, the metadata column map, and the device-transfer INSERT. (Shot-save INSERT binding intentionally omitted — taste is only ever a post-hoc metadata edit, never set at save, so it defaults unset.)
- [x] 1.3 Extend `updateShotMetadataStatic` to accept `tasteBalance` / `tasteBody` keys, validating against the allowed sets (`sour|balanced|bitter`, `thin|medium|heavy`; "" = unset) and dropping out-of-set values without failing the update.
- [x] 1.4 Add `tasteBalance` / `tasteBody` to `ShotRecord`, `ShotProjection` (`.h` + `toVariantMap`/`fromVariantMap`), and `convertShotRecord`, with `Q_PROPERTY` MEMBER exposure for QML.

## 2. Shared picker component

- [x] 2.1 Create `qml/components/TastePicker.qml` — three optional rows (Extraction chips, Body chips, Overall via reused `RatingInput`), tap-only, **no text field**. Emits `*Modified` signals + two-way value props; `show*` flags gate each row.
- [x] 2.2 Register `TastePicker.qml` in `CMakeLists.txt` (`qt_add_qml_module` file list).
- [x] 2.3 Accessibility: chips use `AccessibleMouseArea` (accessibleName + accessibleChecked); Overall uses `RatingInput` (already accessible). Focus order Extraction → Body → Overall follows layout order.
- [x] 2.4 Internationalize all labels via hidden `Tr` instances (`tasteIntake.*` keys).

## 3. Review-page embedding (single UI, single storage)

- [x] 3.1 Embedded `TastePicker` on `PostShotReviewPage` right below the rating (Overall hidden — the existing `RatingInput` owns it), with `editTasteBalance`/`editTasteBody` edit-state seeded on load, in the dirty-check, undo snapshot/restore, `saveEditedShot` metadata write + in-memory clone, and `buildVisualizerOverrides`. Autosave on tap → `pendingVisualizerUpdate` (set in `saveEditedShot`) drives the Visualizer PATCH (satisfies 6.4).
- [x] 3.2 No parallel widget: Overall reuses the one `RatingInput`; the picker adds only the two new axes.

## 4. Advisor first-open gate

- [x] 4.1 Add `SettingsAI::tasteIntakeOnAsk` (bool, default `true`); expose as `Settings.ai.tasteIntakeOnAsk`; add the **"Ask how it tasted"** `StyledSwitch` to `SettingsAITab.qml` so users can turn it off.
- [x] 4.2 `ConversationOverlay.openWithShot` gates the intake on `Settings.ai.tasteIntakeOnAsk && !mistake && !tasteIntakeSeen(shotId)`, seeding the `show*` flags from which axes are unfilled; OFF → opens conversation directly as today. (Both PostShotReviewPage and ShotDetailPage route through `openWithShot`, so both are covered.)
- [x] 4.3 All axes filled → all `show*` flags false → picker renders no rows, leaving the lone "Ask" (+ Skip) — the one-tap question.
- [x] 4.4 "Ask" (`submitIntake()`) persists tapped values via `requestUpdateShotMetadata`, composes a first-person question (English, AI-facing), and sends with the shot attached through the existing `sendFollowUp()`. Nothing selected → sends the plain "What do you think?".
- [x] 4.5 "Skip" sets `intakeVisible = false`, dropping into the normal text conversation with the shot attached.
- [x] 4.6 `SettingsAI::tasteIntakeSeen`/`markTasteIntakeSeen` (QSettings, per shot id) gate the intake to first-open only.

## 5. Prompt + gate wiring

- [x] 5.1 `ShotSummarizer` renders `tasteBalance` / `tasteBody` as prose (## Tasting Feedback) and in the standalone shot JSON; history dial-in blocks (`shotToJson`, `bestRecentShot`) also emit them so the advisor sees prior-shot taste.
- [x] 5.2 Extend the taste-feedback signal: `tastingFeedback` now carries `hasTasteAxis` + values; the empty-feedback gate and system-prompt teaching treat a set taste tap as "user has told us how it tasted" so the advisor doesn't re-ask.
- [ ] 5.3 Verify (manually, in-app) the emergent Layer-1 handoff: fill → advisor gives advice without asking "how did it taste?"; skip → advisor asks and Layer 1 still captures a typed reply.

## 6. Visualizer CVA mapping (upload)

- [x] 6.1 `applyTasteCvaMapping()` in `visualizeruploader.cpp` maps tap enums → CVA (`sour` 12/4, `balanced` 8/8, `bitter` 4/12; `thin`/`medium`/`heavy` → `mouthfeel` 4/8/12), called in the PATCH body (authoritative) and best-effort in the CREATE `.shot` settings. `tasteBalance`/`tasteBody` also flow through the override-application blocks so a just-tapped edit reaches the PATCH.
- [x] 6.2 The helper only ever sets `acidity`/`bitterness`/`mouthfeel`; `sweetness`/`aftertaste`/`aroma`/`flavor`/`fragrance` are never touched (no fabrication).
- [x] 6.3 Never-clear guarantee: a mapped CVA field is written only when the local tap is set and never sent as null, so an untapped shot never clears/overwrites hand-entered CVA. (Cannot detect a remote hand-entry on a shot the user *does* tap without a remote read — the tap is authoritative there; spec/design updated to state this honestly.)
- [x] 6.4 A taste edit triggers the shot PATCH via the existing `PostShotReviewPage.maybeAutoUpdateVisualizer` path (gated on `visualizerAutoUpdate`) — wired by including `tasteBalance`/`tasteBody` in `buildVisualizerOverrides()` and setting `pendingVisualizerUpdate` on a taste change (done in Section 3). No shot-level `touchesVisualizerFields()` exists — that is a coffee-bag concept, not shots.

## 6b. Pre-existing fix: surface silent DB errors (found in review)

- [ ] 6b.1 `ShotHistoryStorage::errorOccurred` had **no consumer** — every DB failure (failed shot save, failed metadata write incl. a taste tap/rating, failed delete/import) was silent, so taste now rode that silent path. Wired it to a non-blocking error toast in `main.qml` (one `Connections` covers all 11 emit sites), mirroring the DE1 `errorOccurred` precedent (#1309).
- [ ] 6b.2 Reworded the two surfaced messages that leaked the internal shot id / used "metadata" jargon (save-metadata, delete) to user-friendly, id-free text; the id + success stay in the qDebug/qWarning diagnostic log.

## 7. Docs

- [x] 7.1 Updated the wiki Manual §12 (AI Assistant) with a **"Taste intake (tap, don't type)"** subsection: the three rows, Ask/Skip, first-open-once behavior, metadata persistence + Visualizer CVA upload, the review-page rows, and the "Ask how it tasted" setting. Drafted locally in `../Decenza.wiki/Manual.md` (separate repo) — **not yet pushed** (awaiting go-ahead).
- [x] 7.2 Update `docs/CLAUDE_MD/AI_ADVISOR.md`: intake surface, structured taste storage, Visualizer CVA mapping, Layer-1 relationship.

## 8. Tests

- [x] 8.1 `tst_dbmigration`: `v32ToV33AddsTasteAxes` (columns added, shots-only, existing rows NULL) + fresh-init/full-chain column assertions.
- [x] 8.2 `tst_shotprojection`: `toVariantMap_roundTripsTasteAxes` (sparse-emit when unset; round-trips when set).
- [x] 8.3 `tst_dbmigration`: `updateShotMetadata_validatesTasteValues` (valid persists; out-of-set dropped with warning without failing a co-present valid field).
- [x] 8.4 `tst_shotsummarizer`: `tasteAxis_countsAsTastingFeedbackAndRendersInProse` (`hasTasteAxis` true + values; prose surfaces taste; not "no tasting feedback").
- [x] 8.5 Visualizer mapping: extracted `applyTasteCvaMapping` to a pure header (`src/network/tastecvamap.h`) and added `tst_tastecvamap` — asserts the value table, the never-null/no-write-when-unset invariant (guards hand-entered CVA), and that the five untouched attributes are never fabricated. Also added a read-path round-trip (`v33_tasteAxesReadRoundTrip`) and extended the device-transfer import test to seed+verify taste (guards the positional bind-order). (PR review follow-ups.)
- [x] 8.6 Built + ran the full suite via Qt Creator: build green (0 errors, 0 warnings); **69/69 test suites pass, 0 warnings**. Fixed the pre-existing latest-schema-version assertions (32 → 33) in `tst_dbmigration`/`tst_coffeebags` that migration 33 bumped.
