## Context

See proposal.md — Why. The state that matters:

- `BeanBaseBlob::markLinkDead` removes `link` and sets `linkChecked` + `linkDead`. It is reached
  from `BagCard.onBagLinkDead` via `blobWithLinkVerdict(raw, "", true)`.
- `BagCard.maybeRecoverArchivedLink` is the only retry after a dead verdict. It requires
  `hasCanonical && !beanBase.link && beanBase.linkDead && beanBase.canonical.link`, so it reads the
  URL out of the canonical snapshot — which a manual bag does not have.
- `imageKey` keys a manual bag's photo on `beanBase.link` being truthy; `BeanBaseDetailsPopup`
  shows its product-link row on `fieldOrEmpty("link").length > 0`. Both currently mean "not dead"
  only because a dead link was deleted.
- `parseArchiveSnapshot` sets `ok = true` as soon as the reply carries an `archived_snapshots`
  object, before looking inside it.

## Goals / Non-Goals

**Goals:**

- A URL the user typed is never destroyed by a verdict.
- A bag holding a dead link keeps asking the archive until it gets one, manual bags included.
- Consumers distinguish "there is a link" from "there is a link worth using".

**Non-Goals:**

- Renaming the marks or moving them under the URL (`linkDeadUrl: "<url>"`). That is the deeper
  shape the altitude review named, and it needs a migration; retaining the URL gets the
  data-loss fix without one, and leaves that door open.
- Cross-checking CDX to decide absence. Treating the empty envelope as unanswered removes the
  wrong verdict without a second API, and CDX was itself serving an outage page during the
  incident, so it is not the more reliable oracle it looks like.
- Any change to what "usable" means. `linkIsUsable` already answers this correctly.

## Decisions

**Keep the URL; the mark carries the meaning.** `linkIsUsable` already returns false for a link
whose `linkDead` names it, so nothing downstream needs the key absent. Deleting it was never
load-bearing — it was how "unusable" was expressed before there was a word for it. Alternative
considered: keep deleting but stash the URL in a second key for recovery — rejected, because that
is `linkDeadUrl` with worse ergonomics and still needs every consumer taught about the new key.

**The retry runs when the bag is USED, not when its card is built.** `BagCard.Component.onCompleted`
fires per card, so a retry hung there would ask the archive about every dead bag every time the
inventory page opens. The existing link check gets away with that only because `linkChecked` is
persisted and settles it once per bag ever; a link that stays retryable has no such stop. The retry
is therefore gated on the bag being the ACTIVE one — `BagCard.selected`, which already exists — so
the cost is one query for the bag you are actually brewing with, at the moment its URL matters
(reordering, photo, details). A dead bag you merely scroll past costs nothing.

**The retained URL is the retry source, replacing the canonical read.** `maybeRecoverArchivedLink`
currently answers "what URL was this bag's, before we cleared it?" by consulting `canonical.link`.
With the URL retained the question disappears — it is `beanBase.link`. Dropping the `hasCanonical`
requirement is then not a widening for its own sake: it is the same retry, sourced from the bag
itself, which is why it starts covering manual bags.

**The 404 decides the mark; the archive only upgrades.** The roaster's 404 already proves the URL
dead, so `validateBagLink` emits `bagLinkDead` on it directly and then asks the archive whether it
can replace the link. Only a capture changes anything; an empty envelope, a 429 and a timeout all
mean the same thing — no replacement yet, ask again when the bag is next used.

An earlier draft instead made the empty envelope "unanswered" so it could not mark a bag dead. That
was wrong in the other direction: archive.org answers a genuinely-unarchived URL with the SAME empty
envelope, so nothing would ever have been marked dead and the "No snapshot exists" case would have
become unreachable. The distinction the draft needed does not exist in the API. Removing the dead
verdict from the archive's answer entirely dissolves the problem instead of trying to read it.

**The dead URL stays visible, marked.** Hiding it is indistinguishable from having deleted it,
which is the outcome the user found surprising. The marking is transient by construction: it shows
only while no capture exists, and a recovering run clears it.

## Risks / Trade-offs

- **A dead link is now visible where previously the row vanished.** → Intended. It is marked, and
  the alternative is silently discarding what the user typed.
- **`link` + `linkDead` together becomes normal, and some reader may still treat a present link as
  usable.** → The mitigation is that `linkIsUsable` already exists and is the single rule; this
  change converts three "is the key there" tests to it. A fourth consumer added later that tests
  presence is the residual risk, and the reason the tests assert usability rather than presence.
- **A never-archived URL is asked about again each time that bag is selected.** → One availability
  request for the ACTIVE bag only, not for every bag whose card is drawn. Accepted deliberately
  over the alternative, which is to trust an answer that has been observed to be wrong.
- **Existing blobs already stamped dead have no URL to retry from.** → They behave exactly as
  today: `canonical.link` still feeds their retry where there is one, and a manual bag already
  emptied stays empty. Nothing regresses; the fix is forward-looking, which is why no migration
  is needed.
