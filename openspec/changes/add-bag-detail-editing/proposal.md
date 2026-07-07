# Add Bag Detail Editing

## Why

We pull rich bean details (origin, region, producer, variety, process, harvest, elevation, tasting notes, URL) from Visualizer's canonical Bean Base, but the user can only *view* them: canonical-supplied fields are read-only, bags not in the canonical database have no way to carry details at all, and a missing product URL cannot be added. Edits also never reach Visualizer — even though Visualizer's own bag editor keeps every one of these fields editable (canonical linking merely autofills) and its API accepts them all on `PATCH /api/coffee_bags/{id}`. Users with Coffee Management maintain the same bag in two places.

## What Changes

- The bag editor (ChangeBeansDialog form, create + edit modes) gains a **Bean details** section exposing the descriptive fields — origin, region, farm, producer, variety, elevation, process, harvest, quality score, place of purchase, tasting notes, and product URL — mirroring Visualizer's own bag editor field set.
- **Every field is editable even while canonical-linked** — identity (roaster, coffee name) and details alike. Canonical data prefills; editing does not break the link (the "verified" badge stays, marking the link rather than locking fields). This replaces the lock-while-linked / "read-only confirmation" rendering in the bag form. Use case: the roaster updated the bag but Visualizer's canonical DB doesn't have the exact entry — link the closest match, then correct it.
- **Revert to Bean Base data**: the pristine canonical entry is snapshotted into the blob at link time (lazily captured for existing bags on first edit), and a revert action restores all canonical-supplied values, discarding local edits.
- A product **URL can be added** when the canonical entry or manual bag has none; it feeds the existing bag-image resolution (`og:image` fetch) and Visualizer's `url` field.
- Edited details are stored in the bag's existing `beanBaseData` blob; a blob **without** a canonical id becomes valid (manual bag with details), so the details popup, bag card attribute line, shot snapshots, AI advisor, and MCP surfaces all pick up user-entered data with no schema change. Three new blob keys: `farm`, `qualityScore`, `placeOfPurchase`.
- **Auto-push to Visualizer**: saving edits to a bag that carries a `visualizerBagId` PATCHes the user's Visualizer bag with the full mapped field set (not just blank-filling). Failed pushes set a pending flag retried on the next upload cycle. Bags not yet on Visualizer are covered by the existing upload-time find-or-create, whose field set expands to include `url`, `farm`, `quality_score`, `place_of_purchase`.
- MCP `bag_update` accepts the detail fields; `bag_list` emits them.

Out of scope: bag image upload (no API field), Visualizer per-user custom metadata fields, the "Get info" URL scraper, pulling server-side bag edits back into Decenza (push is one-way), writes to the canonical database (read-only by design), and BeanInfoPage DYE field locking (unchanged).

## Capabilities

### New Capabilities
- `bag-detail-editing`: editable bean-detail and identity fields in the bag editor — field set, prefill-from-canonical, edit-while-linked, pristine-canonical snapshot + revert, blob merge semantics, URL addition, manual-bag details.

### Modified Capabilities
- `change-beans-dialog`: canonical attributes in the bag form change from read-only confirmation to prefilled editable fields (Bean details section).
- `coffee-bag-model`: `beanBaseData` blob is valid without a canonical id (manual bag with user-entered details); new blob keys `farm`, `qualityScore`, `placeOfPurchase`, plus a `canonical` sub-object holding the pristine entry for revert.
- `bean-base-search`: the "zero footprint" rule keys off an empty blob, not link state — an unlinked bag with user-entered details shows the details row/popup.
- `visualizer-coffee-management`: new requirement — bag edits auto-PATCH the linked Visualizer bag (full values, pending-retry on failure); bag-creation field list expands (`url`, `farm`, `quality_score`, `place_of_purchase`).
- `mcp-server`: `bag_update` accepts bean-detail fields; bag responses emit them.

## Impact

- **QML**: `qml/components/ChangeBeansDialog.qml` (Bean details section), `qml/components/BeanBaseDetailsPopup.qml` (render new keys), `qml/components/BagCard.qml` (unchanged rendering, now fed by manual bags too).
- **C++**: `src/history/coffeebagstorage.{h,cpp}` (blob merge helper, pending-sync flag column), `src/network/visualizeruploader.cpp` (edit-push PATCH path; `buildBagEnrichBody` full-value mode; expanded create body), `src/network/beanbase_blob.h` (new keys), `src/mcp/mcptools_write.cpp` (`bag_update`), `src/mcp/` bag list emit.
- **DB**: one migration — `visualizer_sync_pending` flag on `coffee_bags` (details themselves live in the existing `beanbase_json` column).
- **API**: `PATCH /api/coffee_bags/{id}` (premium-gated; 403/422 handled), field mapping origin↔country, producer↔farmer, process↔processing, harvest↔harvest_time, tastingNotes↔tasting_notes, link↔url.
- **Tests**: coffeebagstorage blob-merge + migration tests, uploader PATCH-body tests against the fake server, MCP register-stub externs (`tst_mcptools_*`).
