## Why

A Decenza user's shots appeared on visualizer.coffee under roasters they had never bought from — "Coava Coffee Roasters" and "Homeground Coffee Roasters" for beans the app correctly showed as "Stavanger Kaffebrenneri". The app was right locally and wrong in the cloud, per bean, indefinitely.

The cause is that **a canonical id is an identity claim, not a details pointer.** `canonical_coffee_bags` rows are a ROASTER'S PRODUCT, and `Shot#refresh_coffee_bag_fields` (`app/models/shot.rb:64-75`, miharekar/visualizer `upstream/main`) overwrites a shot's `bean_brand` and `bean_type` from the linked canonical record whenever the link changes — for any shot with no server-side `coffee_bag`. Decenza let a bag keep a **borrowed** record (the same coffee scraped from a different roaster — often the only match when the user's own roaster is not in the canonical database) while the user corrected the roaster locally. Every upload then re-asserted that borrowed id, and the server renamed the shot.

Reproduced end to end against a live account, through the app's own UI. The affected roaster is not in the canonical database at all (`q=Stavanger` → 0 rows) and the canonical endpoints are read-only, so **no correct id exists for such a bag** — unlinked is the only correct state.

This change also supersedes a spike finding that is now load-bearing and false. `changes/archive/2026-06-20-bean-bag-inventory/design.md` records "GET /api/shots/{id} does NOT return `coffee_bag_id`". It does: `shot/jsonable.rb:71` builds `coffee_bag_id: coffee_bag&.id` and `:45` slices it into `default_json`. The subtlety that matters is that `visualizer_attributes` ends in `attributes.compact` (`:80`), so a bag-less shot **omits** the key rather than sending null — a distinction the shipped repair pass now depends on to decide whether it writes to a user's account at all.

## What Changes

- **The storage layer refuses to hold a conflicted link.** A `coffee_bags` row may carry a canonical id only while its own roaster/coffee still name that record. Enforced at the write, so the bag editor, MCP `bag_update` and the web `/beans` editor all inherit one rule instead of three copies. Editing a bag's IDENTITY breaks the link; editing its DETAILS does not.
- **Existing rows are cleaned by migration**, the unlink propagates to each shot's own snapshot blob (the uploader reads those, not the bag), and uploaded shots are flagged for repair.
- **A bean-repair pass** drains that flag against visualizer.coffee: it reads each shot, decides per shot what may be written, and PATCHes only on a real difference. **BREAKING for cloud data**: it rewrites `bean_brand`/`bean_type` on already-uploaded shots — which is the point, and is why every write is preceded by a read and followed by a read-back.
- **The export gate stays** as a backstop on the shot metadata PATCH: a shot snapshot is a historical record and is never rewritten locally, so its blob may still carry a borrowed id that must not be exported.
- **Corrected API facts of record**: `coffee_bag_id` IS returned by the shot GET but only when non-nil; `canonical_coffee_bag_id` is NOT in `Shot::ALLOWED_ATTRIBUTES`, so no read can confirm an unlink; `GET /api/shots` (the list) renders only `{clock, id, updated_at}`, so it can never answer a bean-identity question; and the API's published rate limits are 50/min per IP and 200/10 min per IP and per user.

## Capabilities

### New Capabilities

_None — every behaviour here refines an existing capability._

### Modified Capabilities

- `canonical-bean-search`: the canonical UUID round-trip gains its missing precondition — the id may be stored and exported only while the local bag's names still match the record, and a borrowed record is dropped rather than corrected.
- `coffee-bag-model`: a `bean_repair_pending` column on `shots`, and a migration that unlinks stored conflicted bags, propagates the unlink to their shots' snapshots, and flags the uploaded ones.
- `visualizer-coffee-management`: the bean-repair pass (queue, pacing, per-shot plan, read-back, failure vocabulary), and the corrected statements about what the shot endpoints actually return.
- `bag-detail-editing`: an identity edit on a linked bag drops the link and keeps every descriptive field the user linked for; a detail edit does not.

## Impact

- **Code**: `src/network/beanbase_blob.h` (the conflict predicate, link stripping, `BagIdentity`/`CanonicalLink`), `src/history/coffeebagstorage.{h,cpp}` (enforcement at both write paths, migration data pass, shot flagging), `src/history/shothistorystorage.{h,cpp}` (migration 38, queue read, flag clear), `src/network/visualizeruploader.{h,cpp}` (repair pass, export gate), `src/controllers/maincontroller.{h,cpp}` (wiring), `src/history/shothistory_types.h` (`BeanRepair`).
- **Data**: one additive column; an in-place unlink of conflicted bags and their shot snapshots. Descriptive fields and the product URL are preserved — only the link keys are removed.
- **A user's visualizer.coffee account**: the repair pass writes to it. It is paced to the server's published limits because shot upload shares that budget, and an unpaced pass can rate-limit a user's actual espresso uploads.
- **Docs**: `docs/CLAUDE_MD/BEAN_BASE.md`.
- **Supersedes**: the `GET /api/shots/{id}` claim in `changes/archive/2026-06-20-bean-bag-inventory/design.md`. Archived changes are historical records and are not edited.
