# bag-detail-editing Specification (delta)

## MODIFIED Requirements

### Requirement: Get info from page (AI extraction)

When a bag has a product URL and an AI provider is configured, the bag editor SHALL offer a "Get info from page" action with two stages. Stage 1: fetch the page locally (following redirects), reduce it to plain text (scripts/styles/svg/img dropped, tags stripped, whitespace squished, length-capped at 48k characters), and have the configured AI extract the details. Stage 2 (fallback): when stage 1 fails with an empty or blocked page, the extraction request SHALL instead ask the configured provider to fetch the URL itself via its web-fetch/web-search tool and return the same JSON contract plus an `imageUrl` key (the main product photo's absolute URL, when the page shows one); when the provider has no web tool, the stage-1 failure surfaces unchanged. The extraction system prompt SHALL be selected by the bag's kind: coffee bags extract `origin, region, farm, producer, variety, elevation, process, harvest, roastLevel, tastingNotes`; tea bags extract `teaType, origin, region, garden, cultivar, flush, tastingNotes, brewTempC, leafGramsPer100Ml, steepTime`, with temperatures normalized to Celsius (°F converted; "boiling"/"freshly-boiled" → 100) and leaf ratio normalized to grams per 100 ml. Extracted values SHALL fill ONLY fields that are currently empty — never overriding user- or canonical-supplied values — and SHALL never be guessed beyond what the page states. The extraction SHALL complete via dedicated signals, never via the advisor's `recommendationReceived`. Failures (unreachable page, unreadable response, AI busy/unconfigured) SHALL surface as an inline status message; the action is hidden without a URL or configured AI.

#### Scenario: Extraction fills empty fields only
- **WHEN** the user taps Get info with tasting notes already entered and origin empty, and the page states both
- **THEN** origin SHALL be filled and the existing tasting notes SHALL be unchanged

#### Scenario: Page states nothing extractable
- **WHEN** the AI returns an object with no whitelisted fields
- **THEN** the form SHALL be unchanged and the status SHALL say nothing new was found

#### Scenario: No AI configured
- **WHEN** no AI provider is configured
- **THEN** the Get info action SHALL NOT be shown

#### Scenario: JS-rendered shop falls back to provider fetch
- **WHEN** the local fetch of a product URL yields under 100 characters of text
- **THEN** the extraction retries through the provider's web-fetch tool and, on success, fills fields exactly as stage 1 would

#### Scenario: Tea page with Fahrenheit
- **WHEN** a tea bag's page states "Brewing Temp: 212º" and "5 minutes"
- **THEN** the blob receives brewTempC 100 and steepTime "5 minutes"

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
