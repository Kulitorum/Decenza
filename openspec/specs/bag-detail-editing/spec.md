# bag-detail-editing Specification

## Purpose
Governs the bag editor's "Bean details" section: every bag field stays editable regardless of Bean Base canonical link state, edits merge into the `beanBaseData` blob without breaking the link, a pristine canonical snapshot enables a "Revert to Bean Base data" action, manual bags resolve a photo from their product URL, and an AI-powered "Get info from page" action fills empty fields only.

## Requirements

### Requirement: All bag fields are editable in the bag editor, linked or not

Every field in the bag editor's "Bean details" section SHALL stay editable regardless of canonical link state, including roaster, coffee name and roast level. A canonical link autofills and shows a "verified" badge but never locks a field. A detail edit SHALL never break the link.

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

#### Scenario: Every detail field is editable on a linked bag

- **GIVEN** a linked bag
- **WHEN** the user edits product URL, origin, farm, producer, quality score, place of purchase or roast level
- **THEN** every edit SHALL be accepted and the link SHALL remain

### Requirement: An identity edit SHALL drop the link only when it names a different coffee

An identity edit (roaster or coffee name) SHALL drop the canonical link when it leaves the bag naming a different coffee than the record's pristine `canonical` names. An empty name on either side is not a disagreement. Dropping the link SHALL remove only the link keys and SHALL keep every descriptive field and the product URL.

#### Scenario: An empty record name is not a disagreement

- **GIVEN** a linked bag whose record has an empty coffee name
- **WHEN** the user edits the roaster only
- **THEN** the canonical link SHALL be kept

### Requirement: Bean details section is collapsed by default

The Bean details section SHALL render collapsed, showing the existing one-line summary (origin · variety · process) when any detail value exists, and SHALL expand to the full field set on demand. An empty section header SHALL still be shown so details can be added to a bag that has none.

#### Scenario: Bag with no details
- **WHEN** the bag editor opens for a bag whose blob carries no detail values
- **THEN** the collapsed Bean details section SHALL be visible and expandable so values can be entered

### Requirement: A product URL can be added or corrected

The Bean details section SHALL include the product URL (`link`), which feeds bag-image resolution and the details popup's open-at-roaster affordance. A write that changes `link` SHALL re-open the link check for the new URL, whatever wrote it, and SHALL drop the `linkChecked` and `linkDead` marks that describe the old URL. Once-per-run guards on link validation and archive lookup SHALL be keyed by URL, not by bag.

#### Scenario: Adding a URL to a bag without one
- **WHEN** the user enters a product URL for a bag whose blob has no `link` and saves
- **THEN** the blob SHALL carry the URL
- **AND** bag-image resolution SHALL be attempted for the bag using the new URL

#### Scenario: A dead URL the user typed is kept
- **WHEN** a manual bag's product URL is found dead and the archive has no capture of it
- **THEN** the URL SHALL remain on the bag, marked dead
- **AND** it SHALL be shown in the details popup marked as no longer resolving

#### Scenario: A dead link drives no photo fetch
- **WHEN** a bag's retained `link` is marked dead
- **THEN** no bag-image resolution SHALL be attempted for it
- **AND** "Get info from page" SHALL NOT be offered for it

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

### Requirement: A dead product URL SHALL be retained and marked, not removed

A URL found dead SHALL be retained on the bag, marked dead, because it may be the only record of where the bag came from. Consumers SHALL gate on whether the link is usable, not on whether the key is present: a dead link SHALL NOT drive photo resolution or be offered to "Get info from page".

#### Scenario: The details popup marks a retained dead URL

- **WHEN** a bag holds a retained dead `link`
- **THEN** the details popup SHALL show the URL marked as no longer resolving
- **AND** once the URL is recovered or replaced, the marking SHALL disappear

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

Saving the bag editor SHALL merge edited fields into the bag's existing `beanBaseData` blob, preserving untouched keys (`id`, `visualizerCanonicalId`, `canonicalRoasterId`, the `canonical` snapshot, `description`, legacy `image`). Identity edits SHALL also update the blob's working `roasterName` and `roastName`. Cleared fields SHALL be removed from the blob, not stored as empty strings.

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

#### Scenario: New detail keys are stored

- **WHEN** the user enters a farm, a quality score and a place of purchase and saves
- **THEN** the blob SHALL carry the `farm`, `qualityScore` and `placeOfPurchase` keys

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

When an AI provider is configured, the bag editor SHALL offer a "Get info from page" action, and SHALL hide it only when no provider is configured or the bag has no URL and the provider cannot search. Extracted values SHALL never be guessed beyond what the page states. Extraction SHALL complete via dedicated signals, never `recommendationReceived`. Failures SHALL surface as an inline status message.

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

### Requirement: Extracted values SHALL be applied by the field's current state

An extracted value SHALL fill an empty field. It SHALL replace a field that matches its `canonical` value, and the replacement SHALL be reported. It SHALL NEVER overwrite a field that differs from `canonical` (user-edited) or a bag with no `canonical` snapshot. The roaster's page SHALL outrank Bean Base, and extraction SHALL leave `canonical` untouched.

#### Scenario: A manual bag's user-entered value is never replaced

- **WHEN** a manual bag holds a user-entered value that the page contradicts
- **THEN** that field SHALL NOT be replaced

### Requirement: Extraction SHALL request the field set for the bag's kind

The extraction prompt SHALL be chosen by bag kind. Coffee bags SHALL extract origin, region, farm, producer, variety, elevation, process, harvest, roastLevel and tastingNotes. Tea bags SHALL extract teaType, origin, region, garden, cultivar, flush, tastingNotes, brewTempC, leafGramsPer100Ml and steepTime, with temperatures in Celsius and leaf ratio in grams per 100 ml.

#### Scenario: Coffee bag requests coffee fields

- **WHEN** Get info runs for a coffee bag
- **THEN** the extraction SHALL request the coffee field set and no tea fields

### Requirement: Get info SHALL fetch the page locally, then fall back to provider fetch

Stage 1 SHALL fetch the page locally, following redirects, and reduce it to plain text with scripts, styles, svg and img dropped and length capped at 48k characters. When stage 1 fails with an empty or blocked page, the request SHALL ask the provider to fetch the URL with its web tool and return the same JSON plus an `imageUrl`. With no web tool, the stage-1 failure SHALL surface unchanged.

#### Scenario: Boiling water is normalised to 100 C

- **WHEN** a tea page states "boiling" as the brewing temperature
- **THEN** the blob SHALL receive brewTempC 100

### Requirement: A dead product URL is replaced by its most recent working form

`link` SHALL mean the most recent URL known to serve the bag's product page. Every bag holding a URL, linked or manual, SHALL get the once-per-bag link check, keyed by the bag (its canonical id, or `bag-<rowid>`). When the stored URL is dead, the Internet Archive SHALL be queried for a snapshot. A snapshot SHALL replace `link` and SHALL NOT mark the bag dead, with no extra blob key.

#### Scenario: A manual bag's URL is checked like any other
- **WHEN** a bag with no `canonical` snapshot holds a product URL
- **THEN** that URL SHALL be link-checked once, as a Bean-Base-linked bag's URL is
- **AND** a dead result SHALL mark it and attempt archive recovery

#### Scenario: Delisted product recovers its page
- **WHEN** a bag's stored product URL returns 404 and the Internet Archive holds a successful
  snapshot of it
- **THEN** the bag's `link` SHALL become the snapshot URL
- **AND** the bag SHALL NOT be marked as having a dead link

#### Scenario: No snapshot exists
- **WHEN** a bag's stored product URL is dead and the Internet Archive has no successful snapshot
- **THEN** the bag SHALL be marked dead
- **AND** the URL SHALL be retained so a later run can ask again

#### Scenario: A manual bag recovers when it is next used
- **WHEN** a bag with no `canonical` snapshot holds a link marked dead, is selected as the active
  bag, and the archive answers with a capture of it
- **THEN** the link SHALL become the snapshot URL and the dead mark SHALL be cleared

#### Scenario: Scrolling past a dead bag costs nothing
- **WHEN** the bag inventory is shown and it contains bags holding links marked dead
- **THEN** no archive lookup SHALL be issued for a bag that is not the active one

#### Scenario: A degraded availability answer marks nothing dead
- **WHEN** the availability API answers with an empty `archived_snapshots` envelope
- **THEN** the bag SHALL NOT be marked dead on the strength of that answer
- **AND** a bag already marked dead SHALL remain retryable rather than being treated as settled

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

### Requirement: Extraction SHALL reach the archive when the page is gone

When the page fetch for extraction finds the URL gone, it SHALL query the archive and extract from a snapshot when one exists. This SHALL hold however the URL reached the field, restored, typed or suggested. A link already pointing at an archive snapshot is terminal and SHALL NOT be re-probed or re-recovered.

#### Scenario: Extraction from a snapshot is attempted for an unchecked URL

- **WHEN** Get info runs on a URL not stored on the bag and the page returns 404
- **THEN** extraction SHALL be attempted from the archive snapshot when one exists

### Requirement: The link check SHALL decide the dead mark, and the archive only upgrades it

A 404 or 410 from the roaster SHALL set the dead mark on its own. The archive SHALL then be asked whether a capture exists, and only a capture SHALL change anything. Every other outcome, including an empty envelope, SHALL leave the bag marked dead and retryable. The availability API SHALL NOT be used to decide absence.

#### Scenario: A 404 with no capture is marked dead on its own

- **WHEN** the link check returns 404 and the archive has no capture
- **THEN** the bag SHALL be marked dead and the URL retained

### Requirement: A dead verdict SHALL be retried when the bag is used

The archive SHALL be asked again for a bag holding a dead link, manual bags included. A successful retry SHALL replace the link and clear the mark. The retry SHALL run when the bag becomes the active bag, not when its card is drawn.

#### Scenario: Retry is not repeated for a bag whose card is drawn

- **WHEN** the bag inventory is drawn with a dead-marked bag that is not active
- **THEN** no archive lookup SHALL be issued for that bag

### Requirement: A bag pursues its photo and details through every available source

A bag's photo and detail resolution SHALL work down an ordered ladder until one rung succeeds or the ladder is exhausted: (1) the live `link`, (2) its Internet Archive snapshot when dead, (3) an AI-found product page when no usable link exists, (4) the AI's `imageUrl` when a page was read with no `og:image`. Each rung SHALL be attempted at most once per bag, and a failed rung SHALL fall through to the next.

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

### Requirement: Photo and details SHALL be pursued together

A rung that yields a readable page SHALL satisfy both photo and details. A photo found without details, or the reverse, SHALL NOT stop the other from continuing.

#### Scenario: A photo without details does not stop details

- **WHEN** a rung yields an `og:image` but no readable detail fields
- **THEN** the detail fields SHALL still be pursued through the next rung

### Requirement: Rungs 1 and 2 SHALL work without an AI provider

Rungs 1 and 2 SHALL require no AI: the photo SHALL come from the page's `og:image`, live or archived. With no provider configured the app SHALL still recover the artwork, and only detail extraction is unavailable. An unconfigured provider SHALL never reduce what the deterministic rungs deliver.

#### Scenario: Unconfigured provider keeps the artwork rungs

- **WHEN** no AI provider is configured and the bag's link is live
- **THEN** the photo SHALL resolve from the page's `og:image`

### Requirement: A missing product URL can be found by the configured AI

When a bag has no usable product URL, the app SHALL ask the configured AI provider, and only the one the user selected, to find the roaster's product page using its web-search tool. "No usable URL" SHALL mean no URL at all, or one already known dead. An automatic search SHALL run at most once per bag. A search the user explicitly requests SHALL always be performed.

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

### Requirement: A declined automatic search SHALL be logged with its condition

An automatic search declined by any condition SHALL record in the session log which condition declined it. A provider with no web-search tool SHALL report that, and SHALL NOT answer from memory.

#### Scenario: A provider without a search tool reports it

- **WHEN** the selected provider has no web-search tool
- **THEN** the search SHALL report that condition and no URL SHALL be taken from the model's memory

### Requirement: A found URL SHALL be probed and confirmed before it is stored

A returned URL SHALL be probed and discarded only when the probe proves it dead; an inconclusive probe SHALL still offer it. The user SHALL confirm the URL before it is stored as `link`. Once confirmed it SHALL feed photo and detail resolution exactly like a typed URL.

#### Scenario: A confirmed URL that the probe cannot settle is stored

- **WHEN** the returned URL's probe is inconclusive and the user confirms it
- **THEN** the URL SHALL be stored as the bag's `link`

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

When a photo is resolved from an archive snapshot, resolution SHALL attempt the original asset URL named in the snapshot first, and SHALL fall back to the archive-proxied URL when the original is unreachable. All existing image-cache rules (cache key, size cap, atomic write, eviction) SHALL apply to whichever URL succeeds.

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
