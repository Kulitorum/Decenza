## MODIFIED Requirements

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
