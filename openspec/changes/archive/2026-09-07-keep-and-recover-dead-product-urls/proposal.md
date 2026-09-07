## Why

A **manual bag** — one not linked to a Bean Base record, so it has no `canonical` snapshot —
is never link-checked at all. `validateBagLink` has two callers, both in `BagCard`, and both
require `hasCanonical`. So a manual bag with a product URL the user typed is never probed,
never marked, and never archive-recovered: when the roaster retires that page the bag simply
stops resolving a photo, silently and permanently. Two such bags exist in the reporter's own
data (Harney & Sons "Decaf Ceylon", entered by hand, `linkChecked` never set).

The second problem is that the verdict a linked bag does get is not as reliable as the code
treats it. Observed on the DE1 (build 3585), one session, same URL:

```
[    56.997] [BeanBase][Extract] … unreadable - Error transferring
             https://web.archive.org/web/20260106073238id_/…  status code 429
[254236.742] [BeanBase][Extract] … unreadable, no archived copy - …  status code 404
```

The first line proves a capture exists. The second reports none. Reproduced outside the
app while archive.org was partially degraded:

```
archive.org/wayback/available          → HTTP 200  {"archived_snapshots": {}}   (×3, consistent)
web.archive.org/web/20260106073238id_/ → HTTP 200, 56275 bytes
cdx …?url=https://…                    → "Internet Archive: Temporarily Offline" (HTML)
cdx …?url=http://…                     → valid JSON, the capture is listed
```

`parseArchiveSnapshot` grants "answered" to any well-formed `archived_snapshots` envelope,
so a degraded **HTTP 200 meaning "I cannot tell you"** is read as a confirmed no-capture.
There is no error status anywhere for the existing fault/miss discipline to catch, and the
result is a bag stamped dead on an answer that was never given.

Third, the existing retry is hung on `BagCard.Component.onCompleted`, so every bag already
marked dead queries the archive **every time the inventory is drawn**. The once-per-bag link
check gets away with that only because `linkChecked` is persisted and settles it; a dead bag
has no such stop.

The three share one fix. Check every bag's URL, not only a linked one; keep the URL when the
verdict is dead, so the bag names its own retry source and a manual bag can recover without a
`canonical` to read; and spend that retry when the user reaches for the bag rather than when
its card is drawn.

An earlier draft of this proposal justified retaining the URL as preventing data loss on a
manual bag. That was wrong and is recorded here so it is not reasoned from again: nothing
deletes a manual bag's URL today, because nothing ever checks it.

A bag on the reporter's DE1 was found holding `link` **and** `linkDead` together, a state
the current writers are not supposed to produce. How it arose is not established. Under
this change that combination stops being a contradiction and becomes the normal
representation, so the question stops mattering.

## What Changes

- **Every bag's product URL is checked, linked or not.** The check is keyed by the bag rather
  than by its canonical id — `bag-<rowid>` for a manual bag, the same key its photo cache
  already uses — so a URL the user typed gets the same probe, dead mark and archive recovery
  a Bean Base link gets.
- **A dead verdict keeps the URL and marks it.** `link` is no longer removed; `linkDead`
  records that it did not resolve. The blob keeps naming the last URL the bag had.
- **Recovery retries from the dead URL itself**, for manual bags as well as canonical ones,
  and runs when the bag is USED rather than when its card is drawn. The moment a capture
  appears the link becomes the snapshot and the bag is an ordinary working bag again.
  Replacing a dead URL with its archived copy is the outcome to reach whenever it is
  reachable at all.
- **Consumers gate on "usable", not "present".** The photo chain and the details popup's
  product-link row read `linkIsUsable` rather than "the key exists", so a dead link neither
  drives a doomed photo fetch nor appears as an ordinary link.
- **The details popup still shows a dead URL, marked as gone.** The user typed it; deleting
  it or hiding it silently is the surprising outcome. Once recovered it renders normally,
  with no marking — the marked state is only ever visible while no capture exists.
- **An empty `archived_snapshots` envelope stops being proof of absence.** It is treated as
  an unanswered question, so it never produces a dead verdict on its own.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `bag-detail-editing`: two requirements change — "A dead product URL is replaced by its most
  recent working form" (every bag is checked, not only a linked one; the verdict keeps the URL;
  recovery retries from it, covers manual bags, and runs on use rather than on draw; an empty
  availability envelope is not a confirmed no-capture); and "A product URL can be added or
  corrected" (a dead link is retained and shown marked rather than removed).

## Impact

- `src/network/beanbase_blob.h` — `markLinkDead` keeps `link`; `linkIsUsable` unchanged.
- `src/network/beanbaseclient.{h,cpp}` — `parseArchiveSnapshot`'s empty-envelope outcome;
  `validateBagLink`'s dead arm.
- `qml/components/BagCard.qml` — a `linkKey` (canonical id, else `bag-<rowid>`) so a manual bag
  can be checked and its signals matched; `maybeValidateLink` and `maybeRecoverArchivedLink`
  drop `hasCanonical`; the retry moves from construction to selection; `imageKey` gates on usable.
- `qml/components/BeanBaseDetailsPopup.qml` — the product-link row's marked state.
- `src/network/shotserver_bags.cpp` — the same "usable, not present" rule on the web surface.
- Tests: `tests/tst_beanbaseclient.cpp`.
- No migration: existing blobs with `linkDead` and no `link` keep working — they simply have
  nothing to retry from until a canonical link supplies one, exactly as today.
