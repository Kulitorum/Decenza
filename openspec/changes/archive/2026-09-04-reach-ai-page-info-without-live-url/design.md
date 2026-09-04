## Context

See proposal.md — Why. The relevant current state is three separate gates, each correct in
isolation:

- `BeanBaseClient::fetchPageText()` builds its request from `archiveRawForm(url)`, which is a
  *rewriter*, not a lookup: it converts an already-archived URL to its `id_` form and returns
  anything else unchanged. Nothing in the extraction path ever asks the archive a question.
- Archive lookups are reached only from `validateBagLink()` and `lookupArchivedLink()`, both entered
  from `BagCard`'s three `maybe*` functions, all of which require a `link` on the blob.
- `ChangeBeansDialog.maybeFindProductPage()` returns when `fLink` is non-empty, and the Get info row
  is `visible:` on the same condition inverted. Between them, exactly one of the two AI paths is
  ever available, decided solely by whether the field has any text in it — dead or alive.

`queryArchiveSnapshot(canonicalId, url, done)` carries a per-bag one-shot (`m_archiveAttempted`,
keyed on `canonicalId`) and refuses a caller with no valid cache-filename id. `fetchArchiveAvailability(url, done)`
is the same lookup without the id or the one-shot.

## Goals / Non-Goals

**Goals:**

- One rule decides which AI affordance is offered, expressed in terms of whether a *working* page is
  reachable — not whether a string is present.
- The archive is reachable from the extraction path itself, independent of how the URL arrived.
- The automatic search says in the log why it did not run.

**Non-Goals:**

- Probing a URL while the user is still typing it. The check runs on a SAVED link, as it does today;
  what changes is which saved links are eligible.
- Any change to `applyExtraction`'s correction rules, to `canonical` capture, or to what an AI may
  overwrite.
- Any new blob key. `aiPageSearched` and `linkDead` keep their current meanings.

## Decisions

**Extraction calls `fetchArchiveAvailability`, not `queryArchiveSnapshot`.** The per-bag one-shot in
`queryArchiveSnapshot` is right for an automatic link check — ask once per bag, ever — and wrong
here. The user pressing Get info is an explicit request, and a second press after a transient
failure must not be answered from a flag saying "we already asked about this bag". The extraction
path also has no `canonicalId` to key on: it is handed a URL, which may not be the bag's stored
link. Calling the lower-level lookup keeps the one-shot where it belongs. Alternative considered:
give `queryArchiveSnapshot` a "force" parameter — rejected, because a boolean that disables the
guard at half the call sites is how the guard stops meaning anything.

**Fall back on 404/410 only, not on every transport error.** A timeout, a DNS failure or a TLS error
says the network is unhappy, not that the page is gone; retrying those through the archive would
return a stale snapshot for a page that is merely unreachable right now, and the user would never
learn their connection is broken. `QNetworkReply::ContentNotFoundError` and the HTTP status attribute
are the discriminator, matching how `validateBagLink` already decides.

**One computed condition drives both the button and the automatic search.** A `linkIsUsable`-style
property — "the field holds a URL not known dead" — replaces the two independent `fLink.trim().length`
tests. The Get info row shows when AI is configured and (`linkIsUsable` or the provider can search);
`maybeFindProductPage` runs when `!linkIsUsable`. Expressing it once means the two cannot disagree
about the same bag, which is the defect: today a dead URL satisfies both "has a link, so offer
extraction" and "has a link, so do not search", and the user gets an action that cannot succeed.
"Known dead" is the blob's `linkDead` or a `linkState` of `"dead"` already resolved this session —
deliberately not a fresh probe, which would put a network round trip in a visibility binding.

**The button in no-URL mode reuses the existing suggestion flow verbatim.** It sets the same
`_pageSearchToken`, and `onProductPageFound` still probes and still asks the user to confirm the
host. The only difference from the automatic path is that the press bypasses the `aiPageSearched`
marker and, on confirmation, chains straight into `fetchPageText` rather than waiting for the user
to press Get info a second time. Alternative considered: a separate "Find product page" button
beside Get info — rejected as two buttons for one intent, and the reason the earlier oversized-button
layout was already awkward.

**The link-state marks belong to a URL, so a write that changes the URL drops them.** `linkChecked`
and `linkDead` read as properties of the bag, but each describes one specific URL: leave them
standing across a `link` rewrite and the new URL inherits a verdict that was never about it.
`revertToCanonical` is where this bites — `link` is in `editableKeys()`, so a revert restores the
canonical URL, while `linkChecked`/`linkDead` are not editable keys and survive untouched. The bag
then holds a URL nothing will ever probe, and (when `linkDead` also survives) a blob asserting both
that it has a link and that its link is dead. Clearing them belongs in the blob helper that writes
`link`, not in each caller: revert, the editor's save, and the AI-suggestion accept are three
writers today and the next one will not remember.

**The one-shot guards move from bag-keyed to URL-keyed.** `m_linkValidated` and `m_archiveAttempted`
exist to stop asking the same question twice, and the question is about a URL. Keyed on
`canonicalId` they also refuse a *different* question about the same bag, which is precisely what a
revert or an accepted AI suggestion creates. Keying them on the URL (with the bag id retained in the
key, so two bags sharing a URL still each get their own answer) preserves the guard and removes the
false negative. Alternative considered: clear the bag's entry whenever its link changes — rejected,
because it puts the invalidation at every writer again, which is the mistake being fixed.

**The declined-search log line is `INFO`, not `DEBUG`.** Per LOGGING.md the tier is chosen by
audience: this line exists to be read in a submitted log by someone asking "did the app try?", which
is the connections-view audience, and `DEBUG` would not reach them. It names the condition
(`no provider`, `provider cannot search`, `already searched`, `link usable`, `identity incomplete`)
and nothing else — no URL, no coffee name, since the surrounding lines already identify the bag.

**Surface parity is per-surface, not a shared renderer.** The archive fallback lands inside
`fetchPageText`, so the ShotServer `/beans` extract endpoint and the MCP `bag_extract_details` tool
inherit it with no change. What does need per-surface work is the *availability* rule: the web page's
Get info button and the MCP tool's decision to search must use the same "no usable URL" condition,
which means exporting it rather than re-deriving it in three places.

## Risks / Trade-offs

- **An archive snapshot can be years old, and extraction will treat it as current.** → Accepted, and
  already the accepted position for the recovered-link case: the alternative is a bag with no details
  at all. The correction rules still protect anything the user typed, and Revert still restores the
  Bean Base values.
- **Two network round trips on a dead URL (fetch, then availability, then snapshot fetch).** → Only
  on the failure path, only on an explicit press, and the availability API is small. No change to the
  success path.
- **`web.archive.org` rate-limits (429) under repeated presses.** → The existing three-way outcome
  discipline covers it: a 429 is a fault, not a proven miss, so it surfaces as "could not read the
  page" rather than stamping anything dead. Worth asserting in a test, because reading a fault as a
  miss is the failure this subsystem keeps re-introducing.
- **A revert now costs a probe, and possibly an archive lookup.** → One HEAD on a deliberate user
  action, which is the case the existing per-URL guard was written for. The bag that motivated this
  spent five page fetches reaching no verdict at all.
- **The button now appears on bags where the search will find nothing**, spending a call to learn
  that. → The user pressed it; that is the trade the explicit-request path exists to make. The
  automatic path keeps its once-per-bag marker.
