## Why

The AI detail-extraction ladder was built so a bag with a stale or missing product URL could still
get its photo and its details. On the bag that motivated it, none of it is reachable.

Bag 133 (Prodigal, "Buenos Aires Caturra ESPRESSO") is linked to a Bean Base record via Visualizer,
and its `beanbase_json` carries no `link` key at all. With no URL, the "Get info from page" action
is hidden — by design, but it means the AI cannot be asked to do anything. Restoring the Bean Base
data put a URL back, and that URL is dead:

```
WARN BeanBaseClient: fetchPageText failed for "https://getprodigal.com/products/buenos-aires-caturra-colombia-washed"
 - "…server replied with status code 404"      (×5 in one session)
```

Zero archive queries accompany those failures. `fetchPageText` calls `archiveRawForm(url)`, which
only rewrites a URL *already* on `web.archive.org`; it never asks the archive for a snapshot of a
live URL that has died. So the one requirement that does reach the archive — the once-per-bag link
check — never fired for this bag, because it is gated on a `link` the blob does not have.

The AI product-page search that exists for exactly this situation is locked out too: its guard is
"the form's link field is empty", and after the Bean Base restore the field holds a dead URL. The
spec already has a scenario named "A bag whose URL died still reaches the search"; the
implementation cannot satisfy it while the dead URL is still sitting in the field.

Finally, none of those gates leave a trace. Every early return in `maybeFindProductPage()` is
silent, so a session log cannot answer "was the search attempted, and what stopped it" — which is
why diagnosing this required reading the database rather than the log.

## What Changes

- **The Get info action is offered when there is no URL**, provided the configured provider supports
  web search. In that state it finds the page first (the existing rung-3 search, presented for
  confirmation as it is today) and then extracts from it. An explicit press SHALL ignore the
  once-per-bag `aiPageSearched` marker — that marker exists to stop *automatic* spending, not to
  refuse a user who asked.
- **`fetchPageText` falls back to the Internet Archive.** On a 404/410 it queries
  `queryArchiveSnapshot` for the URL and, when a snapshot exists, re-fetches it in the `id_` raw
  form. Get info then works on a delisted product page, which is the whole point of having archive
  recovery.
- **The product-page search becomes reachable for a bag whose link is dead**, not only for one whose
  link is absent — closing the gap against the scenario the spec already states.
- **The auto-search records its outcome**, including which condition declined it, so the log can
  answer whether the rung ran.
- **A write that changes `link` re-opens the link check.** Restoring the Bean Base data puts the
  canonical URL back without clearing `linkChecked`/`linkDead`, and both the validation and archive
  one-shots are keyed on the bag rather than the URL — so a restored dead link is never probed and
  never recovered. The one-shots become per URL, and the marks describing the old URL are dropped
  when a new one is written.
- **The ShotServer `/beans` surface and the MCP `bag_extract_details` tool follow**, so the three
  surfaces do not drift on which of these paths is available.

No new blob key. No change to the correction rules, to `canonical` handling, or to what an AI is
permitted to overwrite.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `bag-detail-editing`: four requirements change — "Get info from page (AI extraction)" (the action
  is no longer hidden without a URL when the provider can search); "A dead product URL is replaced
  by its most recent working form" (extraction reaches the archive too, not only the link check);
  "A missing product URL can be found by the configured AI" (reachable when the stored URL is dead,
  and on demand regardless of the once-per-bag marker); and "A product URL can be added or corrected"
  (a write that changes `link` re-opens the link check for the new URL).

## Impact

- `src/network/beanbaseclient.{h,cpp}` — `fetchPageText` archive fallback; per-URL one-shots.
- `src/network/beanbase_blob.h` — dropping the link marks when `link` changes.
- `qml/components/ChangeBeansDialog.qml` — Get info visibility and two-mode behaviour,
  `maybeFindProductPage` guard, status wiring.
- `src/ai/aimanager.cpp` — outcome logging for the product-page search.
- `src/network/shotserver_bags.cpp` — web-surface parity.
- `src/mcp/mcptools_ai.cpp` — MCP parity for a bag with a dead link.
- Tests: `tests/tst_beanbaseclient.cpp` (archive fallback on 404).
- Wiki manual: the Get info entry, if the user-visible rule changes.
