# bag-detail-editing Specification

## Purpose
Governs the bag editor's "Bean details" section: every bag field stays editable regardless of Bean Base canonical link state, edits merge into the `beanBaseData` blob without breaking the link, a pristine canonical snapshot enables a "Revert to Bean Base data" action, manual bags resolve a photo from their product URL, and an AI-powered "Get info from page" action fills empty fields only.

## Requirements

### Requirement: All bag fields are editable in the bag editor, linked or not

The bag editor (ChangeBeansDialog form, create and edit modes) SHALL expose a "Bean details" section with editable fields: product URL, origin, region, farm, producer, variety, elevation, process, harvest, quality score, place of purchase, and tasting notes. Every field — including identity (roaster, coffee name) and roast level — SHALL be editable regardless of canonical link state: a canonical link autofills and shows a "verified" badge, but never locks a field (matching Visualizer's own bag editor).

A **detail** edit SHALL never break the link. An **identity** edit MAY: the canonical id is a claim that this bag IS that roaster's product, so when an edit leaves the bag's roaster or coffee naming a different coffee than the record does, the link SHALL be dropped rather than kept and corrected. The comparison SHALL use the record's pristine names (the `canonical` snapshot where present, since the working identity keys are themselves user-editable), and an empty name on either side SHALL NOT be treated as a disagreement. Dropping the link removes only the link keys; every descriptive field and the product URL — the data the user linked FOR — SHALL be kept as the user's own.

This is not a UI restriction. visualizer.coffee rewrites a shot's `bean_brand` and `bean_type` from the linked canonical record, so a bag that keeps a record naming another roaster's coffee renames every shot it has ever pulled, in the cloud, while the app keeps showing the right name.

#### Scenario: Editing a canonical-linked bag's details

- **WHEN** the user opens the bag editor for a bag linked to a canonical entry and changes the tasting notes and elevation
- **THEN** the edited values SHALL be saved on the bag
- **AND** the canonical link (`beanBaseId`) SHALL remain intact

#### Scenario: Correcting the roaster to one the record does not name

- **WHEN** the user edits a linked bag's roaster from the record's roaster to their own (the borrowed-record case: the same coffee, scraped from a different roaster, was the only match)
- **THEN** the edit SHALL be saved
- **AND** the canonical link SHALL be dropped, because the record no longer describes this bag
- **AND** every descriptive value and the product URL SHALL be kept

#### Scenario: Correcting a linked bag whose roaster entry is stale

- **WHEN** the roaster has updated the coffee (e.g. new crop name) but the canonical DB carries only the older entry, and the user edits the linked bag's coffee name to the new one
- **THEN** the edits SHALL be saved and the blob's working identity keys (`roasterName`, `roastName`) and the bag columns SHALL both reflect the edit
- **AND** the canonical link SHALL be dropped, because the bag now names a coffee the record does not — the app cannot tell a renamed crop from a different product, and the safe answer is the one that cannot rename the user's cloud history

#### Scenario: Adding details to a manual bag

- **WHEN** the user opens the bag editor for a bag with no canonical link and enters origin, variety, and process
- **THEN** the values SHALL be saved and rendered on the bag card attribute line and details popup exactly as canonical data would be

#### Scenario: Prefill from canonical data

- **WHEN** the bag carries canonical-supplied detail values
- **THEN** the Bean details fields SHALL open prefilled with those values as editable text, not read-only confirmation

### Requirement: Bean details section is collapsed by default

The Bean details section SHALL render collapsed, showing the existing one-line summary (origin · variety · process) when any detail value exists, and SHALL expand to the full field set on demand. An empty section header SHALL still be shown so details can be added to a bag that has none.

#### Scenario: Bag with no details
- **WHEN** the bag editor opens for a bag whose blob carries no detail values
- **THEN** the collapsed Bean details section SHALL be visible and expandable so values can be entered

### Requirement: A product URL can be added or corrected

The Bean details section SHALL include the product URL (`link`). When saved, the URL SHALL feed the existing bag-image resolution (`og:image` fetch and file cache) and the details popup's open-at-roaster affordance.

A write that changes `link` SHALL re-open the link check for the new URL, whatever wrote it — the
user typing one, a confirmed AI suggestion, or a restore of the Bean Base data. The marks that
describe a link's state (`linkChecked`, `linkDead`) describe ONE URL, so a write that replaces the
URL SHALL drop them; leaving them standing describes the old URL and silently exempts the new one
from ever being probed or archive-recovered. For the same reason the once-per-run guards on link
validation and on the archive lookup SHALL be keyed by URL, not by bag: their purpose is to stop
asking the same question about the same URL twice, and a bag whose URL has changed is a different
question.

#### Scenario: Adding a URL to a bag without one
- **WHEN** the user enters a product URL for a bag whose blob has no `link` and saves
- **THEN** the blob SHALL carry the URL
- **AND** bag-image resolution SHALL be attempted for the bag using the new URL

#### Scenario: Restoring the Bean Base data restores a dead URL
- **WHEN** the user reverts a bag to its Bean Base data and the canonical record's URL is dead
- **THEN** the restored URL SHALL be probed
- **AND** it SHALL be replaced by an archive snapshot when one exists, or marked dead when none does

#### Scenario: A new URL is probed even though the old one was checked
- **WHEN** a bag already marked as link-checked has its `link` replaced with a different URL
- **THEN** the mark SHALL be dropped and the new URL SHALL be probed

#### Scenario: A bag whose URL changed can ask the archive again
- **WHEN** a bag's URL was already looked up in the archive this run, and the bag's `link` is then
  replaced with a different URL that turns out to be dead
- **THEN** the archive SHALL be asked about the new URL

#### Scenario: The same URL is not re-probed
- **WHEN** a bag's `link` is rewritten to the value it already held
- **THEN** no additional link check or archive lookup SHALL be issued

### Requirement: Pristine canonical snapshot enables revert

On the first edit-save of a linked blob without a `canonical` key, the pre-edit flat values SHALL be copied into a `canonical` sub-object before edits apply — the working values are pristine until the first edit by construction, so this single lazy-capture path covers new links and bags linked before this feature alike. Flat top-level keys remain the working copy consumers read; the `canonical` sub-object is never modified by edits.

#### Scenario: Linked bag gets its snapshot on first edit
- **WHEN** a linked bag (new or pre-feature) is edited and saved for the first time
- **THEN** the pre-edit flat values SHALL be copied into `canonical` before the edits are applied
- **AND** subsequent edits SHALL leave `canonical` untouched

### Requirement: Revert to Bean Base data

When a bag is linked and its working values differ from the `canonical` snapshot, the editor SHALL offer a "Revert to Bean Base data" action. Reverting SHALL restore every canonical-supplied value (identity, roast level, details) over the working keys and remove working detail keys the canonical entry lacked — including a user-added URL — after a confirmation stating that local edits are discarded. A revert is a save: it persists like any edit and triggers the Visualizer edit-push.

#### Scenario: Revert restores canonical values
- **WHEN** the user edited a linked bag's coffee name and tasting notes, then taps Revert and confirms
- **THEN** the name and tasting notes SHALL return to the canonical entry's values
- **AND** the bag row, blob working keys, and display surfaces SHALL all reflect the canonical values

#### Scenario: Revert removes user additions canonical lacked
- **WHEN** the user added a URL the canonical entry did not carry, then reverts
- **THEN** the `link` key SHALL be removed along with the other local edits

#### Scenario: Revert hidden when nothing differs
- **WHEN** a linked bag's working values equal its `canonical` snapshot
- **THEN** no revert affordance SHALL be shown

#### Scenario: Manual bag has no revert
- **WHEN** the bag has no canonical link
- **THEN** no revert affordance SHALL be shown

### Requirement: Edited details merge into the beanBaseData blob

Saving the bag editor SHALL merge edited fields into the bag's existing `beanBaseData` blob, preserving untouched keys (`id`, `visualizerCanonicalId`, `canonicalRoasterId`, the `canonical` snapshot, `description`, legacy `image`). Identity edits SHALL update the blob's working `roasterName`/`roastName` alongside the bag columns. Fields cleared by the user SHALL be removed from the blob (absent, not empty string). New blob keys `farm`, `qualityScore`, and `placeOfPurchase` complement the existing detail keys.

#### Scenario: Merge preserves the link and snapshot
- **WHEN** a linked bag's fields are edited and saved
- **THEN** the blob's `id`, `canonicalRoasterId`, and `canonical` snapshot SHALL be unchanged
- **AND** only the edited working keys SHALL differ

#### Scenario: Clearing a field removes the key
- **WHEN** the user clears the region field and saves
- **THEN** the blob SHALL NOT contain a `region` key
- **AND** the details popup SHALL omit the region row (zero footprint per field)

#### Scenario: Downstream consumers see edited data
- **WHEN** a shot is saved after bag details were edited
- **THEN** the shot's `beanbase_json` snapshot, the AI advisor bean context, and MCP `shots_get_detail` SHALL carry the edited values

### Requirement: Manual bags resolve a photo from their product URL

A bag without a canonical link but with a `link` SHALL resolve its photo through the same og:image pipeline as linked bags, under an image-cache key of `bag-<rowid>`. The canonical URL-recovery fallback SHALL NOT run for such keys (there is no canonical entry to re-search). When og:image resolution fails and a stage-2 extraction returned an `imageUrl`, the image download/cache SHALL consume that URL exactly as it consumes an og:image hit.

#### Scenario: Manual bag with a URL shows a photo
- **WHEN** a manual bag carries a product URL whose page has an og:image
- **THEN** the bag card and details popup SHALL show the resolved photo

#### Scenario: Manual bag without a URL
- **WHEN** a manual bag has no `link`
- **THEN** no image resolution SHALL be attempted and the placeholder remains

#### Scenario: SPA page photo via extraction
- **WHEN** a bag's page has no og:image but the stage-2 extraction returned an `imageUrl`
- **THEN** the bag card shows the downloaded product photo

### Requirement: Get info from page (AI extraction)

When an AI provider is configured, the bag editor SHALL offer a "Get info from page" action. The action SHALL be shown when the bag has a product URL, and ALSO when it has none but the configured provider supports web search — in that second state the action first finds the product page (under the product-page-search requirement, including its confirmation step) and then extracts from the page it found. The action SHALL be hidden only when no AI provider is configured, or when the bag has no URL and the provider cannot search. Stage 1: fetch the page locally (following redirects), reduce it to plain text (scripts/styles/svg/img dropped, tags stripped, whitespace squished, length-capped at 48k characters), and have the configured AI extract the details. Stage 2 (fallback): when stage 1 fails with an empty or blocked page, the extraction request SHALL instead ask the configured provider to fetch the URL itself via its web-fetch/web-search tool and return the same JSON contract plus an `imageUrl` key (the main product photo's absolute URL, when the page shows one); when the provider has no web tool, the stage-1 failure surfaces unchanged. The extraction system prompt SHALL be selected by the bag's kind: coffee bags extract `origin, region, farm, producer, variety, elevation, process, harvest, roastLevel, tastingNotes`; tea bags extract `teaType, origin, region, garden, cultivar, flush, tastingNotes, brewTempC, leafGramsPer100Ml, steepTime`, with temperatures normalized to Celsius (°F converted; "boiling"/"freshly-boiled" → 100) and leaf ratio normalized to grams per 100 ml. Extracted values SHALL never be guessed beyond what the page states. The extraction SHALL complete via dedicated signals, never via the advisor's `recommendationReceived`. Failures (unreachable page, unreadable response, AI busy/unconfigured) SHALL surface as an inline status message.

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

#### Scenario: A bag with no URL is still offered the action
- **WHEN** a bag has no product URL and the configured provider supports web search
- **THEN** the Get info action SHALL be shown
- **AND** pressing it SHALL search for the product page, present it for confirmation, and on
  confirmation extract from it

#### Scenario: A bag with no URL and a provider that cannot search
- **WHEN** a bag has no product URL and the configured provider has no web-search tool
- **THEN** the Get info action SHALL NOT be shown

#### Scenario: An explicit press is not refused by the once-per-bag marker
- **WHEN** the bag is already marked as having been searched for a product page, and the user
  presses the Get info action
- **THEN** the search SHALL be performed
- **AND** the marker SHALL NOT suppress it, because that marker governs automatic spending only

#### Scenario: JS-rendered shop falls back to provider fetch
- **WHEN** the local fetch of a product URL yields under 100 characters of text
- **THEN** the extraction retries through the provider's web-fetch tool and, on success, applies fields exactly as stage 1 would

#### Scenario: Tea page with Fahrenheit
- **WHEN** a tea bag's page states "Brewing Temp: 212º" and "5 minutes"
- **THEN** the blob receives brewTempC 100 and steepTime "5 minutes"

### Requirement: A dead product URL is replaced by its most recent working form

`link` SHALL mean "the most recent URL known to serve this bag's product page". When the app's
once-per-bag link check finds the stored URL dead, it SHALL query the Internet Archive for a
snapshot of that URL before treating the link as lost. When a snapshot exists, the snapshot URL
SHALL replace `link` and the bag SHALL NOT be marked dead; when no snapshot exists, the existing
dead-link handling applies unchanged. No additional blob key SHALL be introduced for the archived
form — every consumer of `link` (photo resolution, "Get info from page", the open-at-roaster
affordance) SHALL use the recovered URL exactly as it used the original.

Reading a page for extraction SHALL reach the archive on its own, not only through the link check.
When the page fetch for extraction finds the URL gone, it SHALL query the Internet Archive for a
snapshot of that URL and, when one exists, extract from the snapshot instead of failing. This holds
however the URL reached the field — restored from the Bean Base record, typed by the user, or
suggested by the AI — because a URL that never passed the once-per-bag link check would otherwise
have no route to the archive at all.

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

#### Scenario: Get info recovers a URL the link check never saw
- **WHEN** the user presses Get info on a URL that is not stored on the bag and that returns 404,
  and the Internet Archive holds a successful snapshot of it
- **THEN** the extraction SHALL proceed from that snapshot
- **AND** the user SHALL NOT be told the page could not be read

#### Scenario: Get info on a dead URL with no snapshot
- **WHEN** the page fetch for extraction finds the URL gone and the archive has no capture of it
- **THEN** the failure SHALL surface as an inline status message naming the page as unreadable

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
page for that coffee, using the provider's own web tool. "No usable URL" SHALL mean the bag has no
URL at all, OR holds one already known to be dead: a bag whose stored URL died is exactly the bag
this rung exists for, so the presence of a dead string in the field SHALL NOT lock the search out.
The request SHALL be made at most once per bag automatically: a search that returns nothing SHALL
NOT be repeated on a later launch, so the feature cannot bill the user twice for the same question.
A search the user asks for explicitly SHALL always be performed — the once-per-bag marker governs
automatic spending only, and refusing a user who pressed the button is not what it protects.

The provider's **web-search** tool SHALL be used, not its fetch-a-named-URL tool: only one
provider's tool does both, and a fetch tool asked to FIND a page answers from memory, which is a
hallucinated URL wearing a tool's credibility. A provider with no search tool SHALL report that
rather than answer.

A returned URL SHALL be probed before it is offered, and SHALL be discarded only when the probe
PROVES it dead. An inconclusive probe is not evidence, so the URL SHALL still be offered — the
user, who is shown the host and must accept, is the check that matters. The user SHALL confirm it
before it is stored as the bag's `link`: a model's guess is not evidence either, and an unconfirmed
guess written into `link` would be read by every downstream consumer as fact.

Once confirmed, the URL SHALL feed photo resolution and detail extraction exactly as a URL the user
typed. A URL that is itself dead SHALL fall through to archive recovery like any other.

An automatic search that is declined by any of its conditions SHALL record which condition declined
it, so a session log answers whether the rung was attempted. A rung that leaves no trace cannot be
diagnosed from a submitted log, which is the only evidence available for a bag on a user's device.

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
- **WHEN** the configured provider returns no URL, or one the probe proves dead
- **THEN** the bag's own fields SHALL be left unchanged apart from the
  already-searched marker
- **AND** the search SHALL NOT be repeated for that bag on a later launch

#### Scenario: A bag whose URL died still reaches the search
- **WHEN** a bag's stored URL was found dead and the archive had no capture
- **THEN** the AI product-page search SHALL still be attempted for it

#### Scenario: A dead URL left in the field does not lock out the search
- **WHEN** a bag's field holds a URL that is known dead, rather than having been cleared
- **THEN** the AI product-page search SHALL still be available for that bag

#### Scenario: A search completing does not save the editor's unsaved edits
- **WHEN** the user changes the bag's link or identity while the search is in
  flight, and the search then returns nothing
- **THEN** only the already-searched marker SHALL be written
- **AND** the unsaved edits SHALL remain unsaved, so Cancel still discards them

#### Scenario: The probe reaches no verdict
- **WHEN** a returned URL's probe cannot resolve either way
- **THEN** the URL SHALL still be offered for confirmation
- **AND** the suggestion SHALL NOT be left pending with nothing shown

#### Scenario: A provider that cannot search
- **WHEN** the selected provider has no web-search tool
- **THEN** the search SHALL report that, and no answer SHALL be taken from the model's memory

#### Scenario: Found URL is itself delisted
- **WHEN** the AI returns a URL that resolves as dead
- **THEN** archive recovery SHALL apply to it exactly as to a stored link

#### Scenario: A declined automatic search is visible in the log
- **WHEN** the automatic product-page search is declined because a condition is not met
- **THEN** the log SHALL record that it was declined and which condition declined it

The "already asked" fact SHALL be recorded under its OWN key, never by reusing
the dead-link mark: a bag whose stored URL died is exactly the bag this rung
exists for, and one key cannot mean both "the URL died" and "stop looking".
Recording it SHALL patch the STORED blob only — a bag editor holding unsaved
edits must not have them persisted by a background search completing.

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
