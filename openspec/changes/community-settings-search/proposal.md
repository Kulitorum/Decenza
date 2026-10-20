## Why

When you open a new bag, Decenza should show what settings work for that bean — what *you* pulled last time, and (eventually) what the community pulled on Visualizer — and let you act on it without leaving the app. Today that answer lives on the Visualizer Community page in a browser, and the Visualizer **API cannot search community shots by bean at all** (`GET /api/shots` switches to your own shots once authenticated; the public feed is an unfiltered id list). So any in-app "community consensus" claim could only ever be invented, not measured. This change begins closing that gap, honestly, from the pieces that need no server work.

## What Changes

Six phases (see `design.md`); **Phase 1 is the deliverable of this change**, the rest are planned follow-on changes:

- **Phase 1 (this change) — Decenza-only, no Visualizer change:** a `CommunitySuggestionsCard.qml` on `BeanInfoPage` that, for the active **coffee** bag, shows your own ranked profiles and last grind for that bean (reusing `ShotHistoryStorage::requestRankedProfilesForBean` / `requestLatestGrindForBean`) and an **"Open on Visualizer"** button that opens the filtered Community page via a deep link. No network client, no aggregator, no settings, no apply path, no community data fetched in-app — the title names honestly what it shows today.
- **Phase 2 (planned):** `CommunityAggregator::summarize()` — pure, table-tested, no network.
- **Phase 3 (planned, separate upstream Rails track):** `GET /api/community/shots` on a Visualizer fork.
- **Phase 4 (planned):** `CommunitySettingsClient` against that endpoint, behind `communityApiBaseUrl`.
- **Phase 5 (planned):** one-tap Apply, factored out of `applyLoadedShotMetadata`, confirmation sheet.
- **Phase 6 (planned):** MCP/web parity + opt-in cited web research via the user's chosen `AIProvider`.

## Upstream intent

The **Bean Search capability is intended for upstream submission to `Kulitorum/Decenza`** (the AI Barista is *not* — it stays private fork IP). Therefore the whole track is built **upstream-clean**: standard-Decenza conventions, on its own files/paths, with **zero dependency on `src/barista/*`**. MCP/web parity (Phase 6) routes through the upstream surfaces (`src/mcp/mcptools_beansearch.cpp`, `src/network/shotserver_bags.cpp`), never the barista tools. Development commits to the `backup` remote; the decision to push upstream is the owner's, later. The Visualizer server endpoint (Phase 3) is a separate upstream track to `miharekar/visualizer`. (Resolves the research doc's open-question #3 → yes.)

## Capabilities

### Added Capabilities

- `bean-community-search`: Decenza surfaces, for the active coffee bag, the user's own profiles and last grind for that bean and a deep link to the filtered Visualizer Community page. (Phase 1; later phases add real community data, aggregation, apply, and parity.)

## Impact

- **New:** `qml/components/CommunitySuggestionsCard.qml`.
- **Changed (additive, minimal — kept cherry-pickable for upstream):** `qml/pages/BeanInfoPage.qml` (card instance + `activeBagIsCoffee` derivation), `CMakeLists.txt` (one line in `qt_add_qml_module`).
- **Reused as-is:** `ShotHistoryStorage::requestRankedProfilesForBean` / `requestLatestGrindForBean` (and their `*ForBeanReady` signals), `Settings.dye.*`, `AccessibleButton`, `Theme`, `TranslationManager`, `Qt.openUrlExternally`.
- **NOT touched:** `src/barista/*`, any C++ (no new types, no settings domain changes), the MCP surface, navigation (`AppShell`).
