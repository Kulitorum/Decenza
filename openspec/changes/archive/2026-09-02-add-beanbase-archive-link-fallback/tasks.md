## 1. Archive lookup in BeanBaseClient

- [x] 1.1 Add the availability-API lookup to `BeanBaseClient` (request the closest capture for a
      URL, parse `archived_snapshots.closest` for `available`/`status`/`url`), with the archive host
      as an overridable base so `tst_beanbaseclient`'s stub server can serve it. Verify with a new
      test slot in `tst_beanbaseclient.cpp` covering: a 200 capture, an `available:false` answer, a
      malformed body, and a transport failure — each mapping to hit / miss / miss / *silent*.
- [x] 1.2 Add a helper that derives the `id_` raw-bytes form of a snapshot URL from its ordinary
      form, and its `im_` asset form. Verify with a pure unit test over both derivations, including
      a URL that already carries a modifier (must not be double-applied).
- [x] 1.3 Re-point `validateBagLink()`'s 404/410 arm at the lookup: emit a new
      `bagLinkArchived(canonicalId, snapshotUrl)` on a hit, the existing `bagLinkDead(canonicalId)`
      only on a confirmed no-capture, and nothing at all when the archive itself failed. Verify by
      extending `validateBagLinkDeadOn404` and adding the archived and archive-unreachable cases —
      the last asserting no signal of either kind is emitted.

## 2. Blob handling in BagCard

- [x] 2.1 Handle `bagLinkArchived` in `BagCard.qml`: set `link` to the snapshot URL, stamp
      `linkChecked: true`, ensure no `linkDead` is left on the blob, and persist. Verify on device by
      pointing a bag at a URL known to 404 with a capture, and confirming the blob afterwards via
      MCP `bag` list.
- [x] 2.2 Recover bags already stamped dead: when `linkDead` is set and `canonical.link` exists,
      run one lookup for that URL and, on a hit, clear `linkDead` and set `link`. Leave
      `maybeRecoverLink()`'s `!linkDead` guard alone. Verify against the live Prodigal "Buenos Aires
      Caturra" bag — it must come back with a `web.archive.org` link and lose `linkDead`.
- [x] 2.3 Make recovery force a photo re-resolve. Picking a Bean Base entry with a stale URL fires
      `ensureBagImage` at pick time, which fails and stamps the once-per-session attempt guard — so
      recovery must go through the forcing path (as `refreshBagImage` does) or the bag stays
      photo-less until the next launch. Verify by picking a known-dead entry and confirming the
      photo appears without restarting the app.
- [x] 2.4 Confirm the photo and "Get info from page" both light up for a recovered bag (both key off
      a non-empty `link`), with no change to either code path.

## 3. Photo resolution through a snapshot

- [x] 3.1 Fetch snapshot pages with the `id_` form in the photo chain so `extractOgImage()` reads
      the original asset URL unchanged. Verify with a test that serves an archived page body whose
      `og:image` is an origin URL and asserts the origin asset is the one downloaded.
- [x] 3.2 Fall back to the `im_` asset form when the original asset is unreachable. Verify with a
      test where the origin asset 404s and the proxied asset serves bytes — the cached file must be
      the proxied bytes.
- [x] 3.3 Confirm the existing size cap, atomic write and eviction rules are untouched by adding an
      oversized-asset case to the archived path.

## 4. Link state in the Bean Base picker

- [x] 4.1 Add per-URL link-state probing to `BeanBaseClient` (HEAD, GET fallback on 405; 404/410
      escalates to one availability lookup), with a session cache keyed by URL and a cap on in-flight
      probes. Verify with tests for each state transition and one asserting a repeated query issues
      no second probe.
- [x] 4.2 Consume the state in `ChangeBeansDialog.qml`: order within each existing tier by live >
      archived > none, treating unresolved as live, re-sorting in place as answers arrive. Verify
      manually with a search that returns the two Prodigal Buenos Aires entries plus a live-link
      coffee — the live one must sit above both.
- [x] 4.3 Show the state on each result row alongside the source label, distinguishing "not yet
      known" from "no usable link". Verify manually on device, including the moment before the
      probes resolve.

## 5. AI product-page search (last rung)

- [x] 5.1 Add a product-page search to `AIManager`: ask the **selected** provider (never a
      substitute, never when unconfigured) to find the roaster's product page for a given
      roaster + coffee name via its web tool, returning a single URL or nothing. Complete on its own
      signals like `bagDetailsExtracted` does, not via the advisor's `recommendationReceived`.
      Verify with tests over the response parse: a good URL, an empty answer, prose instead of a
      URL, and a non-http scheme.
- [x] 5.2 Wire it as the last rung of the resolution ladder: attempted only after the live and
      archive routes are exhausted, at most once per bag, stamping `linkDead` on a miss so it is
      never repeated. Verify with a test asserting a second run for the same bag issues no provider
      call.
- [x] 5.3 Present a found URL for confirmation with its host legible, and store it only on accept.
      Verify manually: accept writes `link` and triggers photo plus extraction; decline writes
      nothing and does not re-offer.
- [x] 5.4 Confirm a found-but-dead URL falls through to archive recovery, so the AI rung composes
      with rung 2 rather than bypassing it.
- [x] 5.5 Assert the AI rungs are additive only: with no provider configured, a live link and a
      dead-link-with-capture must both still produce a cached photo. Verify with a test that runs
      the ladder with the AI manager absent and asserts the image file lands.

## 6. Correcting stale Bean Base values

- [x] 6.1 Add field-level provenance to the merge: a flat value equal to its `canonical` counterpart
      is Bean-Base-sourced, one that differs is user-edited, and a blob with no `canonical` is
      wholly user-entered. Verify with unit tests over the three shapes, including a blob whose
      `canonical` lacks the key entirely.
- [x] 6.2 Change the extraction apply rule from fill-empty-only to: fill empty, replace
      Bean-Base-sourced values the page contradicts, never touch user-edited values or a manual
      bag's fields. Leave the `canonical` sub-object untouched. Verify with tests for each row of
      the spec's table, plus one asserting Revert still restores the Bean Base values afterwards.
- [x] 6.3 Report corrections to the user — which fields changed, old value to new — rather than
      silently rewriting. Verify manually on a bag whose Bean Base process disagrees with its
      roaster page.
- [x] 6.4 Update the two other extraction consumers so the apply rule is stated once, not three
      times: the ShotServer bag page (`shotserver_bags.cpp`) and the MCP tool
      (`mcptools_ai.cpp`) must not keep a fill-empty-only copy of the merge.

## 7. ShotServer and MCP parity

- [x] 7.0 Keep the web and MCP surfaces level with the app: `/api/beans/search` returns and
      orders by `linkState` and the picker renders the chip; the web editor gains the
      product-page search (`POST /api/beans/findpage`) with the same offer-then-confirm shape;
      corrections are reported with field labels, not blob keys. `bean_search` carries
      `linkState`, and `bag_extract_details` reports `applied`/`corrections` plus a
      `suggestedUrl` for a bag with no link. `McpSurfaceVersion` bumped.

## 8. Verification and delivery

- [x] 8.1 Run the full test suite through the Qt Creator MCP (`run_tests`, scope `all`) and confirm
      no new failures and no new warnings.
- [x] 8.2 Add the wiki manual entry. The archive recovery is invisible automation and stays
      undocumented, but two surfaces are things a user must be told exist: the AI product-page
      suggestion they are asked to confirm (including that it uses their configured provider, once
      per bag), and the picker's link-state indication. Three or four sentences on the Beans page,
      total — cut it in half before committing.
- [x] 8.3 Open the PR, then run `/pr-review-toolkit:review-pr` and address the findings.
- [x] 8.4 Archive the change and sync specs as the final commit on the PR branch.
