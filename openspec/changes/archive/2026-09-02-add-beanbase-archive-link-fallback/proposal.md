## Why

A roaster delists a coffee and its product page 404s. Today that is terminal: the bag card's
once-per-bag link check clears `link`, stamps `linkDead`, and the bag keeps a placeholder photo
forever. "Get info from page" also disappears with the URL, so the AI extraction path is gone too.

The page is usually still on the Internet Archive, and — for Shopify roasters — the product photo
is often still live on the roaster's own CDN even though the page around it is gone. Verified on
Prodigal Coffee's "Buenos Aires Caturra": both Bean Base entries 404, both have 200 Wayback
snapshots, and the `og:image:secure_url` those snapshots carry
(`getprodigal.com/cdn/shop/files/WEBBuenosAires_…png`) still serves 5.1 MB of PNG at 1874×1874 —
under the existing 8 MB `kBagImageMaxBytes` cap.

A Bean Base entry with no URL at all is the same loss by a different route. The URL is not a
convenience for reordering — it is the input to the whole extraction: origin, region, farm,
producer, variety, elevation, process, harvest, roast level and tasting notes for coffee, and the
tea vocabulary including brew temperature, leaf ratio and steep time. Bean Base entries are
routinely sparse; the roaster's own page is where that data actually lives. With no URL there is
nothing to read, so those fields stay empty and the photo stays a placeholder.

Bean Base data can also be simply wrong, and today nothing can fix it. Extraction fills only
fields that are **empty**, so a stale or mistaken canonical value — the wrong process, an outdated
roast level, a variety that was corrected after the record was mirrored — permanently blocks the
roaster's own page from supplying the right one. The page is the more authoritative source for that
roaster's coffee, and the app currently refuses to believe it.

The same coffee also exposed a picker problem. Bean Base returned two entries for it whose every
displayed attribute is byte-identical (origin, region, producer, variety, process, tasting notes,
roast degree). Nothing on the rows told the user that both links were dead, so the pick was a coin
flip between two equally useless records.

## What Changes

- When a bag's stored product URL is found dead, the app SHALL consult the Internet Archive
  availability API before giving up, and adopt the snapshot URL as the bag's `link` when one
  exists. No new blob key: `link` continues to mean "the most recent URL known to work", which
  may now be an archive snapshot.
- Bags **already** marked dead SHALL recover the same way, looking up the original URL their
  pristine `canonical` snapshot still holds — without this the change does not fix the bag that
  prompted it.
- Bag photo resolution SHALL follow that recovered link — reading `og:image` from the snapshot and
  preferring the *original* asset URL it names over the archive's rewritten `im_` proxy, falling
  back to the proxy when the original is gone.
- "Get info from page" SHALL work against an archive-backed link, so AI detail extraction survives
  a delisted product.
- Extraction SHALL correct as well as fill. A field holding a Bean Base value the roaster's page
  contradicts SHALL be replaced and the correction reported; a field the **user** edited SHALL
  never be overwritten. The pristine `canonical` snapshot is left untouched, so Revert to Bean Base
  data undoes a correction like any edit. **BREAKING** relative to the current spec's
  "fill ONLY fields that are currently empty" rule.
- When a bag has no usable URL at all, the app SHALL ask the **configured** AI provider to find the
  roaster's product page for that coffee — once per bag, on the selected provider only. A found URL
  SHALL be probed and shown to the user for confirmation before it is stored, and once stored it
  feeds the full extraction — every detail field and the photo — exactly as a URL the user typed.
- Bean Base search results SHALL be ordered within their existing quality tier by link usefulness:
  live link first, archive-only next, no link last, with a probe fired per result and the list
  re-sorted as answers arrive.
- Result rows SHALL show which of those three states applies, so two otherwise identical entries
  are distinguishable before the user commits to one.

## Capabilities

### New Capabilities

None. This extends behaviour already owned by two specs.

### Modified Capabilities

- `bag-detail-editing`: the product-URL and bag-photo requirements gain an archive fallback — a
  dead link is re-pointed at its Wayback snapshot rather than cleared, photo resolution reads the
  snapshot's `og:image` (original asset URL preferred), and page-text extraction works from the
  snapshot. It also gains an AI product-page search for bags with no URL at all, which is what makes
  the detail extraction reachable for them, and its extraction rule changes from fill-empty-only to
  fill-empty-and-correct-stale-canonical.
- `change-beans-dialog`: the quality-ranked-results requirement gains a within-tier ordering by
  link state (live / archive-only / none) and a visible per-row indication of that state.

## Impact

- `src/network/beanbaseclient.{h,cpp}` — archive lookup, snapshot `og:image` parse, original-asset
  preference in the image chain, per-URL link-state probing and caching for the picker.
- `src/ai/aimanager.{h,cpp}` — a product-page search request against the selected provider's
  web-fetch/web-search tool, completing on its own signals like the existing bag extraction does.
- `qml/components/BagCard.qml` — the 404 arm that currently stamps `linkDead` and stops
  (`BagCard.qml:120-124`) instead attempts recovery first.
- `qml/components/ChangeBeansDialog.qml` — result rows gain the link-state indication; the
  existing tier sort gains the within-tier key.
- New outbound host: `archive.org` (availability API) and `web.archive.org` (snapshot fetch). The
  app already fetches arbitrary roaster pages, but this sends a candidate URL to a third party the
  user did not choose, which the design must bound.
- No schema migration, no new `beanBaseData` key, no settings.
