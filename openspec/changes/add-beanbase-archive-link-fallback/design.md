## Context

See proposal.md — Why.

The machinery this change extends already exists and is small:

- `BeanBaseClient::validateBagLink()` (`src/network/beanbaseclient.cpp:357`) GETs the stored URL
  once per bag per session, follows redirects, and emits `bagLinkDead(id)` on 404/410. A transient
  failure (status 0 or ≥ 500) deliberately emits nothing so a later session retries.
- `BagCard.qml`'s `onBagLinkDead` handler (`qml/components/BagCard.qml:120-124`) deletes `link` and
  stamps `linkDead: true, linkChecked: true`. `linkChecked` is what makes the probe once-per-bag
  rather than once-per-view; `linkDead` is what stops `maybeRecoverLink()` re-adding the same dead
  URL from the canonical record.
- `startBagImageResolve()` → `fetchProductPage()` → `extractOgImage()` → `downloadBagImage()` is the
  whole photo chain, gated by `kBagImageMaxBytes` (8 MB) and a written-file cache with eviction.
- Bean Base search results already carry `link` in each entry, so the picker has a URL to judge
  without an extra lookup.

## Goals / Non-Goals

**Goals:**

- A bag ends up with its artwork and its bean details whenever those exist anywhere reachable —
  live page, archive capture, or a page the configured AI can find.
- A delisted coffee keeps its photo, its details extraction, and a URL that opens something.
- The picker distinguishes two entries that differ only in whether their link still works.
- No new `beanBaseData` key, no schema migration, no settings.

**Non-Goals:**

- Archiving pages ourselves (the Save Page Now API). We read what is there.
- A settings toggle for the AI rung. If the automatic behaviour proves unwanted, that is the fix —
  it is not in this change.
- Searching for a coffee the roaster never put online. The ladder ends; it does not invent.
- Re-checking an archive-backed link on a later launch. Recovery is once, and terminal.
- Offline behaviour. Every path here is best-effort and silent on failure, as the photo chain
  already is.

## Decisions

### Availability API, not CDX

`https://archive.org/wayback/available?url=<url>` answers in one small JSON object with the closest
snapshot and its status. The CDX server returns the full capture history and would need paging and
filtering to answer the only question being asked — "is there a good capture, and where". Verified
against both dead Prodigal URLs; both return `"status": "200", "available": true`.

Alternative considered: probing `https://web.archive.org/web/2/<url>` directly and following the
redirect. Fewer requests, but it conflates "no capture" with "archive is unreachable", and this
design needs those distinguished (see the dead-stamp rule below).

### Fetch snapshots through the `id_` modifier

`https://web.archive.org/web/<timestamp>id_/<url>` returns the **original page bytes** — no archive
toolbar, no URL rewriting. Measured on the Prodigal capture: 56 KB against 304 KB for the rewritten
form, and its `og:image` is already the roaster's own CDN URL
(`getprodigal.com/cdn/shop/files/WEBBuenosAires_…png`) rather than a `…im_/…` proxy.

That makes the spec's "prefer the original asset over the archive proxy" rule fall out of the fetch
choice instead of needing preference logic in `extractOgImage()`, and it keeps the page text fed to
"Get info from page" free of archive chrome. The proxied form remains the fallback: if the original
asset 404s, re-point at `…<timestamp>im_/<original asset url>`.

`id_` serves the bytes with the origin's own `Content-Encoding`. Qt handles that transparently —
`QHttpNetworkConnection` sets `Accept-Encoding` whenever the request does not and flags
`autoDecompress` (`qtbase/src/network/access/qhttpnetworkconnection.cpp:297-302`) — so no
decompression code is needed. (`curl` without `--compressed` shows raw gzip here, which is a curl
default, not an archive quirk.)

### The stored `link` is the human-readable snapshot URL, not the `id_` form

`link` is opened by the details popup's reorder affordance, so it should be the ordinary
`https://web.archive.org/web/<ts>/<url>` — a page a person can read. The `id_` variant is derived
at fetch time by the two machine consumers (photo resolution, page-text extraction). One stored
URL, one derivation rule, no second key. This satisfies "the most recent URL known to work" without
new vocabulary.

### Recovery replaces the dead-stamp, and stamps `linkChecked`

The 404/410 arm of `validateBagLink()` no longer emits `bagLinkDead` directly. It asks the
availability API first:

| Archive answer | Emitted | Blob result |
|---|---|---|
| Snapshot exists | new `bagLinkArchived(id, snapshotUrl)` | `link` = snapshot, `linkChecked: true`, no `linkDead` |
| No capture | `bagLinkDead(id)` (unchanged) | `link` deleted, `linkDead: true`, `linkChecked: true` |
| Archive unreachable / malformed | nothing | blob untouched, retried next session |

A new signal is needed rather than reusing `bagLinkRecovered`: that handler returns early when
`blob.link` is already set (`BagCard.qml:91-92`), which is exactly the case here — the dead URL is
still in the blob at that moment. Overloading it would silently do nothing.

Stamping `linkChecked: true` on the recovered blob is what makes recovery terminal: `maybeValidateLink()`
requires `!linkChecked`, so an archive URL is never probed, never found "dead", and never recovered
twice. It also means no `linkDead` on a recovered bag, which is what re-enables the photo chain and
the "Get info from page" affordance — both of which key off a non-empty `link`.

### One ladder, one runner, not four call sites

The photo chain, the archive recovery, the AI page search and the stage-2 `imageUrl` are four
sources for the same two outcomes (a photo, a filled detail set). They are written as an ordered
ladder with a single runner rather than four handlers that each know what to try next — otherwise
"what do we try after X" is encoded four times and drifts, which is the failure this codebase has
already paid for in the toast markup and the USB-scale prefixes.

The runner owns: which rung is next, that each rung is attempted at most once per bag, and that a
rung's failure falls through rather than ending the attempt. Each source stays a dumb step that
succeeds or does not.

### The AI page search reuses `linkDead` as its "stop looking" marker

"At most once per bag, never repeated on a later launch" needs a persisted fact — without one the
app re-bills the user for the same failed question on every launch. But this change deliberately
does not grow the blob vocabulary, so rather than a new key, a bag whose search found nothing is
stamped `linkDead: true`, widening that key's meaning from "the stored URL is confirmed gone" to
"no usable URL is known — stop looking".

The widening is compatible with every existing reader: `linkDead` already means "do not go looking
for a URL for this bag" to `maybeRecoverLink()`, and a bag with no URL and no prospect of one is
exactly that state. The cost is that a bag stamped this way will not pick up a URL the canonical
record gains later — acceptable against a new key, and reversible by the user editing the URL field,
which clears the state through the existing edit path.

Alternative considered: a dedicated `aiSearchAttempted` key. Cleaner semantics, one more key in a
blob that is copied into every shot snapshot, and a second thing to reason about at revert time.

### The AI rung is automatic, but bounded and single-shot

The user's requirement is to find artwork and details whenever they exist, which a manual button
does not deliver — nobody presses it on a bag they did not know was missing anything. So the rung
runs on its own, subject to three bounds that keep "automatic" from meaning "unbounded spend":

- Only when an AI provider is configured, and only the **selected** one. No substitution, ever, for
  any reason — including a provider erroring or rate-limiting.
- Once per bag, enforced by the marker above. A miss is permanent; there is no retry, no backoff.
- Only on the last rung, after the free deterministic routes have been exhausted.

Confirmation stays in the loop despite the automation: the search may run unattended, but its result
is a suggestion until the user accepts it. That is the line between spending a cent to look and
writing a guess into the record every consumer treats as fact.

### Page beats Bean Base; the user beats both

The apply rule changes from "fill empty fields" to a three-way decision, and the blob already
carries the information needed to make it — no new state. `canonical` is the pristine Bean Base
snapshot and the flat keys are the working copy, so:

- flat value **equals** its `canonical` counterpart → it came from Bean Base, and the page may
  correct it
- flat value **differs** from `canonical` → the user edited it, and it is untouchable
- no `canonical` at all (manual bag) → everything is user-entered, all untouchable

That mapping is exact, which is why this is safe to automate: "did the user type this" is a fact in
the data, not a heuristic. `blobDiffersFromCanonical()` already reasons this way for the Revert
affordance; this is the same comparison at field granularity.

Corrections leave `canonical` untouched, so Revert to Bean Base data undoes a correction exactly as
it undoes a manual edit — the user always has a way back to what Bean Base said.

Alternative considered: present every correction for confirmation. Rejected for canonical-sourced
fields — the user did not enter those values, has no stake in them, and a dialog per stale field
turns a background improvement into a chore. They are reported after the fact instead, which is
what makes them auditable without being a prompt. A user-edited field is the opposite case, and
there the answer is not "confirm" but "never".

### Picker probes are HEAD, bounded, and cached per URL

Each Bean Base result's `link` is probed with HEAD (falling back to GET on 405 — some Shopify
storefronts reject HEAD). 404/410 escalates to one availability lookup; anything else counts as
live. State is cached in a session-scoped `QHash<QString /*url*/, LinkState>` on `BeanBaseClient`,
so re-running a query, or the same coffee appearing in two searches, costs nothing.

Bounded three ways: the existing type-ahead debounce means one probe wave per settled query, not per
keystroke; only results actually in the returned list are probed (typically 2–10); and a hard cap on
in-flight probes keeps a broad query from opening dozens of sockets on a tablet.

Ordering is optimistic — an unresolved result sorts as live. Results therefore appear instantly and
only ever sink as answers arrive, which is far less jarring than rows climbing past each other.

Alternative considered: probe only when the user taps a row, and warn after the fact. Cheaper, but
it does not solve the actual problem — the user cannot tell the two rows apart *before* choosing,
which is the whole complaint.

### Archive lookups happen only for URLs already found dead

No live URL is ever sent to archive.org. This keeps the third-party exposure proportional: the app
already fetches roaster pages directly, and only a URL that has proven itself gone reaches the
archive — one lookup per dead URL per session, deduplicated by the same cache.

## Risks / Trade-offs

- **The AI rung spends the user's provider budget without them asking each time** → One web-search
  call per bag that reaches the last rung, never repeated, only on the selected provider, only when
  one is configured. A user who adds a handful of bags a month pays for a handful of calls. If that
  is still unwanted, the honest control is a settings toggle, which this design does not add — say
  so rather than quietly making the rung manual, which would mean it never runs.
- **A hallucinated page value overwrites a correct Bean Base one** → Bounded by three things: the
  extraction prompt already forbids values the page does not state, the page must be one the bag's
  own link resolves to (not a search result the model picked), and every replacement is reported and
  revertible through the untouched `canonical` snapshot. The exposure is a wrong *canonical* value,
  never a wrong user value.
- **A model can return a plausible wrong product page** → It is probed, shown with its host, and
  written only on confirmation. An unconfirmed URL never reaches `link`, so it never reaches the
  photo cache, the extraction, or a shot snapshot.
- **Widening `linkDead` costs a bag its chance at a canonical URL added later** → Accepted against a
  new blob key; the user's own URL edit clears the state.
- **Picker probing sends HEAD requests to roaster sites for entries the user never picks** → Only
  results of a settled query are probed, capped and cached; the requests carry no identifiers beyond
  a normal fetch, and no user data. Worth noting in the wiki entry as a behaviour change.
- **archive.org is rate-limited and sometimes slow or 5xx** → Every lookup is best-effort and
  non-blocking. Crucially, an archive failure emits *nothing*: it must not be mistaken for "no
  capture", which would permanently stamp a bag dead over a blip. Same rule the existing transient
  branch already follows.
- **A snapshot can be a 404 page captured with status 200, or a cookie/consent interstitial** →
  Nothing usable is extracted from those: no `og:image` means no photo, and the extraction prompt
  already refuses to invent values the text does not state. The failure mode is "no improvement",
  not "wrong data".
- **The recovered `link` no longer buys the coffee** → It is presented as an archived page, and the
  picker's link-state indication says so before the pick. The alternative (dropping the URL) loses
  the photo and the details, which is the status quo being fixed.
- **A roaster who later restores the product page keeps the archive link forever**, since recovery
  is terminal → Accepted: the URL field remains user-editable, and re-probing every archived link on
  every launch is exactly the per-view cost `linkChecked` exists to prevent.
- **`id_` is an Internet Archive implementation detail, not a documented contract** → If it stops
  working, the fetch falls back to the rewritten snapshot URL, which still carries an `og:image`
  (the `im_` proxy) and still yields page text; only fidelity and size regress.

## Migration Plan

No schema change and no stored-data rewrite, but bags **already** stamped `linkDead` must recover
too — otherwise the change does not fix the bag that prompted it. A linked blob keeps the original
URL in its pristine `canonical` sub-object even after the working `link` is deleted: bag 6 in the
live database still holds
`canonical.link = https://getprodigal.com/products/buenos-aires-caturra-colombia-washed` alongside
`linkDead: true`. That is the URL to look up.

So the recovery entry point is not only the 404 arm: a bag with `linkDead` set and a
`canonical.link` present SHALL attempt one availability lookup for that URL, and on a hit clear
`linkDead` and set `link` to the snapshot exactly as the live path does. `maybeRecoverLink()`'s
existing `!linkDead` guard stays — it protects against re-adding the same *dead* URL from the
canonical API, which is a different thing from looking that URL up in the archive.

With no new blob key, a miss cannot be recorded, so an already-dead bag with no capture repeats one
small JSON lookup per launch. Bounded by the number of dead bags (a handful), it costs one request
each, and it self-heals if the page is archived later — which is the better failure direction than
stamping "never look again" into a blob whose vocabulary this change deliberately does not grow.
