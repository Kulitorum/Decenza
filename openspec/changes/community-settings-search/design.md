# Bean Search — design

Full research & design of record (6 phases, options considered, Visualizer server change, risks, sources):
`~/Library/Mobile Documents/com~apple~CloudDocs/Decenza Bean Search Research & Design.md` (Oct 18, 2026).
The Fable Phase-1 design-of-record (fork-adapted, with verified file:line reuse) is
`docs/barista/BEAN-SEARCH-PHASE1-DESIGN-2026-10-19.md`. This file captures the architecture and the
Phase-1 build as implemented; later phases are summarized here and specced in their own changes.

## Fork adaptation

The research doc assumes upstream Decenza + per-phase PRs to Kulitorum. We are a **private fork** (`~/Decenza-fork`):
- Workflow is OpenSpec change + a `docs/barista/` design-of-record + a Fable design pass + owner word-gate on
  model-visible strings + commit to the **`backup`** remote (never origin/community during fork dev).
- **Upstream intent (owner):** the Bean Search capability — *not* the AI Barista — is destined for a PR to
  `Kulitorum/Decenza`. So it is built **upstream-clean**: own files/paths, standard conventions, **no
  `src/barista/*` dependency**, minimal additive edits to shared files so the commits cherry-pick onto a clean
  checkout. The eventual upstream PR follows that repo's `AGENTS.md`/`CONTRIBUTING.md`/`STYLE.md` (open an issue
  first, Qt-Test coverage, an OpenSpec change folder, no MCP-budget violation in the parity phase).
- The Visualizer `GET /api/community/shots` endpoint (Phase 3) is a **separate upstream track** to
  `miharekar/visualizer` (its own Rails PR + Minitest + `openapi.yaml` bump).

## The shape

Three sources feed one card, and only one of them needs the server endpoint:

1. **Your own history** (local `shots.db`) — always available; the Phase-1 content.
2. **Community shots** (Visualizer) — needs Phase 3's endpoint; until then the **deep link** is the community path.
3. **Web research** (your chosen `AIProvider`) — Phase 6, opt-in, cited, never auto-applied.

## Phase 1 (this change) — concrete design

A read-only `CommunitySuggestionsCard.qml`, modelled on `ShotResultsCard.qml`, hosted as the first child of
`BeanInfoPage`'s `contentColumn`. It is **parametrized**, not bound to `Settings` internally, so a later phase can
reuse it for a just-picked bag:

- Props: `beanBrand`, `beanType`, `roastLevel`, `isCoffee`. The host binds brand/type/roast to
  `Settings.dye.dyeBeanBrand`/`dyeBeanType`/`dyeRoastLevel` (the active bag IS the dye state), and derives
  `isCoffee` by matching the active bag id against `inventoryBags` (as `BagCard.qml:32` matches) and reading its
  `kind` — no C++/settings change.
- **Visibility:** `isCoffee && beanBrand !== "" && beanType !== ""`. Community/canonical is coffee-only, so the
  card collapses for tea and when no bean is set; absence is the right empty state on an inventory page.
- **Own-history wiring:** on bean change, `refresh()` clears prior results and calls
  `requestRankedProfilesForBean(brand, type, roast)` and `requestLatestGrindForBean(brand, type, roast)` on
  `MainController.shotHistory`. A single `Connections` consumes `rankedProfilesForBeanReady` /
  `latestGrindForBeanReady`.
  - **Stale-reply guard (mandatory):** `MainController.shotHistory` is a shared singleton; other surfaces
    (ProfilePicker, the recipe wizard) query it too. Each reply echoes its query (`queryBrand`/`queryType`), so a
    reply whose echo ≠ the card's current bean is dropped — the same guard `ProfilePicker.qml:124` uses.
  - Display: up to 3 of `withBean` (`{profileName, lastUsed}`, recency-ordered) as "profile · last used <date>"
    (`lastUsed` is epoch seconds); `similar` is *not* surfaced in Phase 1 (it would imply shots that weren't on
    this bean). The grind line honours `matchLevel`: a `similarRoast` grind is labelled as such, never presented
    as this bean's own. Empty `withBean` and empty grind each collapse their rows; with neither, a one-line "no
    shots logged yet" caption shows above the still-present deep-link button.
- **Deep link (works today):**
  `https://visualizer.coffee/community?commit=Search&bean_brand=<roaster>&bean_type=<coffee>&min_enjoyment=70`,
  each value `encodeURIComponent`'d, opened with `Qt.openUrlExternally`. `min_enjoyment=70` is hardcoded in
  Phase 1 (it becomes the `communityMinEnjoyment` setting in Phase 4).
- **No new log line:** nothing here is a fault a submitted log must reconstruct; the storage layer logs its own
  queries. (A subsystem line earns its place in Phase 4's network client, not here.)

### Honest naming

The available queries return ranked **profiles** + a latest **grind**, not a per-shot list, and no community data
is fetched yet. The card is titled "Your history & Visualizer" (owner-chosen) rather than "Community", and is
renamed toward "Community" when Phase 3/4 make real community data available.

## Later phases (planned, separate changes)

- **Phase 2** `CommunityAggregator::summarize(shots, myGrinder)` — pure static C++, no Qt networking: leading-number
  cleaning, enjoyment-0-as-unrated, profile-family grouping, shrunken-mean ranking, medians/ranges for the top
  profile, grinder match via `GrinderAliases`, ≥3-distinct-users gate, last-12-months preference. Table-driven tests.
- **Phase 3** `GET /api/community/shots` on a Visualizer fork (~40 lines: `Filterable` + visibility scopes +
  existing trigram indexes). Access rules copied from `CommunityController#index`. Separate upstream track.
- **Phase 4** `CommunitySettingsClient` modelled on `BeanBaseClient` (debounce, latest-wins, session cache, fake
  `QTcpServer` tests), `communitySearchEnabled`/`communityMinEnjoyment`/`communityApiBaseUrl` on
  `SettingsVisualizer`, a 404 → deep-link fallback, 429 → retry state.
- **Phase 5** `applyCommunitySuggestion()` factored out of `applyLoadedShotMetadata`, confirmation sheet listing
  every value that will change; grind applied only for a matching grinder.
- **Phase 6** `community` action on the MCP `bean_search` tool + `GET /api/beans/community` on the shot server
  (upstream surfaces) + opt-in cited web research through the user's chosen provider.
