## Why

visualizer.coffee is now a place people edit shots, not just view them: its Journal table (2026-10-02) bulk-edits up to 100 shots at a time, and its notes became rich text (2026-08-02). Decenza only ever pushed, and every automatic update re-sent every field, so a Journal edit was overwritten by the next unrelated edit in Decenza and never came back to the app. Reading Visualizer's source also showed three upload bugs: the barista never arrived (its parser reads `my_name`), multi-line notes collapsed onto one line on every PATCH (the PATCH reads HTML), and shot recovery stored raw HTML as notes. Bag edits, including AI fills, waited for a shot upload to confirm Coffee Management, so on a device that never uploads a shot they never reached Visualizer, and archiving a bag on Visualizer had no effect in Decenza.

## What Changes

- **Send only what changed.** Each shot records which Visualizer fields were edited locally and not yet sent; an automatic update sends only those. The Upload button still sends everything.
- **Pull edits back.** A background pass (startup, every 30 minutes, on account connect) reads shots changed on Visualizer since the last pass and writes their values locally, except fields edited here and not yet sent. A pull never clears a local value.
- **Bag archive syncs both ways.** Finishing a bag in Decenza archives it on Visualizer and returning it to inventory restores it; an archive or restore there does the same here. Visualizer added the API for this at our request (miharekar/visualizer#262).
- **Other bag state comes back.** Bags in inventory are read for a freeze or thaw and for descriptive fields Decenza is missing.
- **Bag photos fill the gap.** A bag photo either side lacks is copied from the other; neither side's photo is replaced.
- **Bag edits push at once** unless Coffee Management is known to be off; parked pushes retry at the end of each pass.
- **Upload fixes:** barista as `my_name`; notes escaped as Markdown on upload, HTML on PATCH, converted back to text on every read; Visualizer's own error text shown for 403/429 and other refusals; an unknown share code says so instead of "Network error".
- The bag photo cache key (canonical id, else `bag-<rowid>`) was hand-written at eight sites; it is now `BeanBaseClient::imageKeyFor()` / `bagImageKey()`.
- The "Auto-update shots" description says it now brings edits back too. No new setting: pulling is governed by the same switch.

## Capabilities

### New Capabilities
- `visualizer-edit-pull`: bringing shot and coffee-bag edits made on visualizer.coffee back to Decenza.

### Modified Capabilities
- `visualizer-auto-update`: an automatic update sends only locally edited fields.
- `visualizer-coffee-management`: a bag edit is pushed without waiting for a shot upload.
- `visualizer-upload-persistence`: barista and notes reach Visualizer as written.

## Impact

- **Database**: migration 43 — `shots.visualizer_dirty`, `shots.visualizer_dirty_seq`, `coffee_bags.visualizer_archived_at`; carried by backup import.
- **C++**: new `VisualizerShotSync` (`src/network/visualizershotsync.*`), pure rules in `visualizersync.h` and `visualizernotes.h`; `VisualizerUploader`, `ShotHistoryStorage`, `CoffeeBagStorage`, `ShotFileParser`, `MainController`.
- **UI / web**: one description string in the Shot Upload tab and its ShotServer counterpart; the bag photo key in `BagCard`, `ChangeBeansDialog`, `RecipesPage` and `RecipeWizardPage` now comes from `bagImageKey()`.
- **Network**: per pass, one shot-list and one bag-list request, plus one read per changed linked shot and per synced bag in inventory, and a one-time photo upload per bag; paced at the existing 4 s interval.
- **Docs**: `docs/CLAUDE_MD/VISUALIZER.md`; wiki manual entry.
