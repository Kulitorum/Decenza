## ADDED Requirements

### Requirement: A dead product URL is replaced by its most recent working form

`link` SHALL mean "the most recent URL known to serve this bag's product page". When the app's
once-per-bag link check finds the stored URL dead, it SHALL query the Internet Archive for a
snapshot of that URL before treating the link as lost. When a snapshot exists, the snapshot URL
SHALL replace `link` and the bag SHALL NOT be marked dead; when no snapshot exists, the existing
dead-link handling applies unchanged. No additional blob key SHALL be introduced for the archived
form — every consumer of `link` (photo resolution, "Get info from page", the open-at-roaster
affordance) SHALL use the recovered URL exactly as it used the original.

A link already pointing at an archive snapshot SHALL NOT be re-probed or re-recovered: it is
terminal, and a snapshot that later becomes unreachable leaves the bag as it would have been.

#### Scenario: Delisted product recovers its page
- **WHEN** a bag's stored product URL returns 404 and the Internet Archive holds a successful
  snapshot of it
- **THEN** the bag's `link` SHALL become the snapshot URL
- **AND** the bag SHALL NOT be marked as having a dead link

#### Scenario: No snapshot exists
- **WHEN** a bag's stored product URL is dead and the Internet Archive has no successful snapshot
- **THEN** the bag SHALL be marked dead exactly as before this change

#### Scenario: A recovered link is not probed again
- **WHEN** a bag whose `link` is already an archive snapshot is displayed
- **THEN** no further link check or archive lookup SHALL be issued for it

#### Scenario: Picking a Bean Base entry whose URL is stale
- **WHEN** the user picks a Bean Base entry whose product URL is dead, so the photo attempt made at
  pick time fails
- **THEN** the resulting bag SHALL still recover through the archive
- **AND** photo resolution SHALL be re-attempted against the recovered URL, rather than being
  suppressed by the failed attempt already made for that bag this session

#### Scenario: Extraction works from the recovered page
- **WHEN** a bag whose `link` is an archive snapshot and an AI provider is configured
- **THEN** "Get info from page" SHALL be offered and SHALL extract from the snapshot's page text
  under the same apply rules as a live page

### Requirement: A bag pursues its photo and details through every available source

Resolving a bag's photo and detail fields SHALL NOT stop at the first source that fails. The app
SHALL work down an ordered ladder until something succeeds or the ladder is exhausted:

1. The bag's `link`, when live — `og:image` for the photo, page text for the details.
2. The Internet Archive snapshot of that link, when the link is dead.
3. A product page found by the configured AI, when there is no usable link by either route above.
4. The AI's own `imageUrl`, when a page was read but stated no `og:image`.

Each rung SHALL be attempted at most once per bag. A rung that fails SHALL fall through to the next
rather than ending the attempt, and a rung that succeeds SHALL end it. When the ladder is exhausted
the bag keeps its placeholder and empty fields exactly as today — the ladder adds attempts, never
failure modes.

Details and photo SHALL be pursued together: a rung that yields a readable page satisfies both,
and a photo found without details (or the reverse) SHALL NOT stop the other from continuing.

**Rungs 1 and 2 require no AI.** The photo comes from the page's own `og:image`, live or archived,
which is a plain fetch. With no AI provider configured the app SHALL still work those two rungs and
SHALL still recover the artwork — only the detail extraction, which is an AI call, is unavailable,
exactly as it is today. An unconfigured provider SHALL never reduce what the deterministic rungs
deliver.

#### Scenario: Live page satisfies both
- **WHEN** a bag's link is live and its page states an `og:image` and detail fields
- **THEN** no further rung SHALL be attempted

#### Scenario: Dead link falls through to the archive
- **WHEN** a bag's link is dead and the Internet Archive holds a capture
- **THEN** the photo and details SHALL be taken from the capture
- **AND** the AI product-page search SHALL NOT be attempted

#### Scenario: No link and no capture reaches the AI rung
- **WHEN** a bag has no usable URL by any route and an AI provider is configured
- **THEN** the AI product-page search SHALL be attempted

#### Scenario: No AI configured still recovers the artwork
- **WHEN** no AI provider is configured and a bag's link is live, or dead with an archive capture
- **THEN** the photo SHALL still resolve from that page's `og:image`
- **AND** only the detail extraction SHALL be unavailable, as it is today

#### Scenario: Exhausted ladder degrades to today's behaviour
- **WHEN** every rung fails or is unavailable
- **THEN** the bag SHALL show its placeholder photo and its fields SHALL remain empty, with no
  error surfaced beyond the existing inline status

#### Scenario: A rung is never retried
- **WHEN** a bag has already attempted a rung in an earlier session
- **THEN** that rung SHALL NOT be attempted again for that bag

### Requirement: A missing product URL can be found by the configured AI

When a bag has no usable product URL, the app SHALL ask the **configured** AI provider — the one
the user selected, never another, and only when one is configured — to find the roaster's product
page for that coffee, using the provider's own web tool. The request SHALL be made at most once per
bag: a search that returns nothing SHALL NOT be repeated on a later launch, so the feature cannot
bill the user twice for the same question.

A returned URL SHALL be probed before use and SHALL be adopted only when it resolves. The user
SHALL be shown the URL, with its host legible, and SHALL confirm it before it is stored as the
bag's `link` — a model's guess is not evidence, and an unconfirmed guess written into `link` would
be read by every downstream consumer as fact.

Once confirmed, the URL SHALL feed photo resolution and detail extraction exactly as a URL the user
typed. A URL that is itself dead SHALL fall through to archive recovery like any other.

#### Scenario: Search finds the page
- **WHEN** a bag has no URL, an AI provider is configured, and the search returns a resolving URL
- **THEN** the URL SHALL be presented for confirmation with its host visible
- **AND** on confirmation it SHALL become the bag's `link` and drive photo and detail extraction

#### Scenario: User rejects the suggestion
- **WHEN** the user declines the suggested URL
- **THEN** nothing SHALL be written to the bag
- **AND** the search SHALL NOT be re-offered automatically for that bag

#### Scenario: No provider configured
- **WHEN** a bag has no URL and no AI provider is configured
- **THEN** no search SHALL be attempted and no provider SHALL be substituted

#### Scenario: Search returns nothing
- **WHEN** the configured provider returns no URL, or one that does not resolve
- **THEN** the bag SHALL be left unchanged
- **AND** the search SHALL NOT be repeated for that bag on a later launch

#### Scenario: Found URL is itself delisted
- **WHEN** the AI returns a URL that resolves as dead
- **THEN** archive recovery SHALL apply to it exactly as to a stored link

### Requirement: Bags already marked dead recover from their pristine snapshot

A linked bag whose working `link` was cleared as dead still carries the original URL in its
pristine `canonical` sub-object. Such a bag SHALL attempt archive recovery from that URL. On a hit,
the dead mark SHALL be cleared and `link` SHALL become the snapshot URL, exactly as for a link that
dies after this change ships. On a miss the bag SHALL remain marked dead and unchanged.

Recovery SHALL NOT re-add a dead URL as a live one: only an archive snapshot may clear the dead
mark.

#### Scenario: A bag marked dead before this change recovers
- **WHEN** a bag is marked dead, its `canonical` snapshot names the original URL, and the Internet
  Archive holds a capture of that URL
- **THEN** the bag's `link` SHALL become the snapshot URL and the dead mark SHALL be cleared
- **AND** its photo and "Get info from page" SHALL become available again

#### Scenario: A bag marked dead with no capture stays dead
- **WHEN** a bag is marked dead and the Internet Archive has no capture of its original URL
- **THEN** the bag SHALL remain marked dead with no `link`

#### Scenario: The dead URL is never restored as live
- **WHEN** archive recovery does not succeed for a bag marked dead
- **THEN** the original dead URL SHALL NOT be written back into `link`

### Requirement: Photo resolution prefers the original asset a snapshot names

When a bag's photo is resolved from an archive snapshot, the snapshot's `og:image` metadata names
both an archive-proxied asset URL and the original asset URL it was captured from. Resolution
SHALL attempt the original URL first — roaster asset hosts commonly outlive the product pages that
referenced them, and the original is the higher-fidelity, lower-latency source — and SHALL fall
back to the archive-proxied URL when the original is unreachable. All existing image-cache rules
(cache key, size cap, atomic write, eviction) SHALL apply unchanged to whichever URL succeeds.

#### Scenario: Original asset still served
- **WHEN** a bag's photo resolves from an archive snapshot whose original asset URL is reachable
- **THEN** the original asset SHALL be downloaded and cached
- **AND** the archive-proxied URL SHALL NOT be fetched

#### Scenario: Original asset gone
- **WHEN** the original asset URL named by the snapshot is unreachable
- **THEN** the archive-proxied asset SHALL be downloaded and cached instead

#### Scenario: Oversized asset is still rejected
- **WHEN** an asset recovered by either route exceeds the bag-image size cap
- **THEN** it SHALL be discarded exactly as an oversized live-page asset is

## MODIFIED Requirements

### Requirement: Get info from page (AI extraction)

When a bag has a product URL and an AI provider is configured, the bag editor SHALL offer a "Get info from page" action with two stages. Stage 1: fetch the page locally (following redirects), reduce it to plain text (scripts/styles/svg/img dropped, tags stripped, whitespace squished, length-capped at 48k characters), and have the configured AI extract the details. Stage 2 (fallback): when stage 1 fails with an empty or blocked page, the extraction request SHALL instead ask the configured provider to fetch the URL itself via its web-fetch/web-search tool and return the same JSON contract plus an `imageUrl` key (the main product photo's absolute URL, when the page shows one); when the provider has no web tool, the stage-1 failure surfaces unchanged. The extraction system prompt SHALL be selected by the bag's kind: coffee bags extract `origin, region, farm, producer, variety, elevation, process, harvest, roastLevel, tastingNotes`; tea bags extract `teaType, origin, region, garden, cultivar, flush, tastingNotes, brewTempC, leafGramsPer100Ml, steepTime`, with temperatures normalized to Celsius (°F converted; "boiling"/"freshly-boiled" → 100) and leaf ratio normalized to grams per 100 ml. Extracted values SHALL never be guessed beyond what the page states. The extraction SHALL complete via dedicated signals, never via the advisor's `recommendationReceived`. Failures (unreachable page, unreadable response, AI busy/unconfigured) SHALL surface as an inline status message; the action is hidden without a URL or configured AI.

An extracted value SHALL be applied according to what the field currently holds, which the blob
already distinguishes: a flat working value equal to its `canonical` counterpart came from Bean
Base, and one that differs from it was edited by the user.

| Current field state | Page states a different value | Behaviour |
|---|---|---|
| Empty | — | Filled |
| Matches the `canonical` snapshot (Bean Base's own value) | yes | **Replaced**, and reported |
| Differs from the `canonical` snapshot (user-edited) | yes | Left alone |
| No `canonical` snapshot (manual bag, user-entered) | yes | Left alone |

The roaster's product page SHALL be treated as more authoritative than the Bean Base record for
that roaster's own coffee: a canonical value the page contradicts is stale or wrong, and correcting
it is the point of reading the page. A user-entered value SHALL NEVER be overwritten — the user
knows something the page does not, and silently discarding that is worse than leaving a field
stale.

Every replacement SHALL be reported to the user: which fields changed, and from what to what. The
existing pristine `canonical` sub-object SHALL be left untouched by extraction, so "Revert to Bean
Base data" undoes a correction exactly as it undoes a manual edit.

#### Scenario: Extraction fills empty fields only
- **WHEN** the user taps Get info with tasting notes the user entered and origin empty, and the page states both
- **THEN** origin SHALL be filled and the user's tasting notes SHALL be unchanged

#### Scenario: Page corrects a wrong Bean Base value
- **WHEN** a bag's process reads "Natural" from Bean Base, matching its `canonical` snapshot, and
  the roaster's page states "Washed"
- **THEN** process SHALL be replaced with "Washed"
- **AND** the change SHALL be reported to the user as a correction, naming the old and new values

#### Scenario: A user-edited value survives the page
- **WHEN** a bag's variety was edited by the user to differ from its `canonical` snapshot, and the
  page states a different variety
- **THEN** the user's value SHALL be kept unchanged

#### Scenario: A manual bag's own values survive
- **WHEN** a bag has no `canonical` snapshot and its fields hold user-entered values the page
  contradicts
- **THEN** no field SHALL be replaced

#### Scenario: A correction is revertible
- **WHEN** extraction has replaced one or more canonical-sourced values
- **THEN** "Revert to Bean Base data" SHALL restore the Bean Base values, the `canonical` snapshot
  having been left untouched

#### Scenario: Page agrees with Bean Base
- **WHEN** every value the page states matches what the bag already holds
- **THEN** nothing SHALL be written and the status SHALL say nothing new was found

#### Scenario: Page states nothing extractable
- **WHEN** the AI returns an object with no whitelisted fields
- **THEN** the form SHALL be unchanged and the status SHALL say nothing new was found

#### Scenario: No AI configured
- **WHEN** no AI provider is configured
- **THEN** the Get info action SHALL NOT be shown

#### Scenario: JS-rendered shop falls back to provider fetch
- **WHEN** the local fetch of a product URL yields under 100 characters of text
- **THEN** the extraction retries through the provider's web-fetch tool and, on success, applies fields exactly as stage 1 would

#### Scenario: Tea page with Fahrenheit
- **WHEN** a tea bag's page states "Brewing Temp: 212º" and "5 minutes"
- **THEN** the blob receives brewTempC 100 and steepTime "5 minutes"
