## 1. Upload fixes

- [x] 1.1 Send the barista as `app.data.settings.my_name`; drop the ignored `barista` keys. Verify: `tst_visualizermixgoal` asserts `my_name`.
- [x] 1.2 Notes per route (`visualizernotes.h`): Markdown-escaped on upload, HTML on shot and bag PATCH, plain text from every read including recovery. Verify: `tst_visualizermixgoal`, `tst_coffeebags`, `tst_visualizershotparse`.
- [x] 1.3 `apiErrorMessage()`: Visualizer's JSON `error` for any refused request, used by upload and update. An unknown share code says "No shared shot has that code".

## 2. Send only what changed

- [x] 2.1 Migration 43 adds `visualizer_dirty`, `visualizer_dirty_seq` (shots) and `visualizer_archived_at` (coffee_bags); backup import carries the shot columns, the bag column rides `kCols`.
- [x] 2.2 `updateShotMetadataStatic` marks the fields whose value an edit changes; a send clears only its fields and only if the seq is unchanged. Verify: `tst_dbmigration::visualizerDirty_marksClearsAndGuardsPulls`.
- [x] 2.3 `sendSavedShot`: automatic update sends the dirty fields, Upload sends all; the rating back-sync sends the rating only.

## 3. Pull edits back

- [x] 3.1 `VisualizerShotSync`: paced pass at startup, every 30 min and on connect; per-account cursor advanced over complete passes only.
- [x] 3.2 Shot pull rules (`shotPullChanges`) and the storage write that applies them around dirty fields. Verify: `tst_visualizershotparse`, `tst_dbmigration`.
- [x] 3.3 Bag pull rules: archive state from the bag list (`bagArchivePullChanges`), freezer and blanks per in-inventory bag (`bagFieldPullChanges`); applied without echoing a push. Verify: `tst_visualizershotparse`.
- [x] 3.4 Bag photos: fill whichever side lacks one (download into the cache / multipart upload).

## 4. Bag edits

- [x] 4.1 Push a bag edit unless CM is known off; retry parked pushes at the end of each pass. Verify: `tst_coffeebags::bagEditPushPolicyByCmState`.
- [x] 4.2 Finishing / restocking a bag pushes `archived_at` when it disagrees with Visualizer's last known state (`bagArchiveForPush`); `inInventory` becomes a Visualizer-pushed field. Verify: `tst_visualizershotparse`, `tst_coffeebags::touchesVisualizerFieldsMembership`.
- [x] 4.3 One bag photo key helper (`BeanBaseClient::imageKeyFor` / `bagImageKey`) replaces the eight hand-written copies.

## 5. Surfaces and docs

- [x] 5.1 Shot Upload tab and ShotServer settings: "Auto-update shots" description mentions bringing edits back.
- [x] 5.2 `docs/CLAUDE_MD/VISUALIZER.md`.
- [ ] 5.3 Wiki manual: a short entry under Visualizer for two-way sync and bag archiving.

## 6. Verify

- [x] 6.1 Full suite through Qt Creator: 119/119 passed.
- [ ] 6.6 Open the Beans, Recipes and Recipe Wizard screens and confirm bag photos still show (the key moved to `bagImageKey()`). The web /beans page already shows them; the three QML screens open by long press.
- [x] 6.7 Mac dev build, 2026-10-05: migration 43 ran on the real desktop database; an edit marks only changed fields, a re-save marks nothing, and an empty value over an unset one is not a change (checked in the DB); the web settings page shows the new description; no QML warnings in the log.

A Mac dev run with the account connected (2026-10-05 08:11) already completed one real pass: 54 changed shots listed, 29 linked read, one roast level pulled with no echo PATCH; the bag list read; one defrost date pulled; five bag photos uploaded and visible on visualizer.coffee; a second pass read only the one shot changed since. This exercised every request shape against the live server, but it is not a substitute for 6.2-6.5.

Held for the next beta: 6.2-6.5 run against Jeff's one paid Visualizer account, so they happen on his production Android tablet, not on a desktop or simulator build, which would fill that account with test data. They are not passed until that beta confirms them.

- [ ] 6.2 Live (beta, tablet): edit a shot's grind and rating in Visualizer's Journal, confirm both arrive in Decenza within a pass; edit a different field in Decenza and confirm the Journal values survive.
- [ ] 6.3 Live (beta, tablet): archive a synced bag on Visualizer, confirm it is marked finished in Decenza; restore it, confirm it is back in inventory. Mark a bag finished in Decenza, confirm it is archived on Visualizer.
- [ ] 6.4 Live (beta, tablet): fill a synced bag with AI before any shot upload that session, confirm the fields reach Visualizer.
- [ ] 6.5 Live (beta, tablet): a synced bag with a cached photo gets it on Visualizer; a Visualizer bag photo appears on a bag that had none.
