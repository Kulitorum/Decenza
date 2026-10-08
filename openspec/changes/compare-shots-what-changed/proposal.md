# Proposal

## Why

The Compare Shots page lists each shot's inputs side by side and leaves the user to find the
difference. Comparing two dial-in shots on the same bean, five of nine rows were identical, the
one input that moved was not marked, and the outcome rows carried only values you entered
(duration, dose, output) — nothing the machine measured, and no difference column. An unrated
shot also reads as an orange "0%".

Decent's own shot history (decentespresso.com/support) answers a better question — *what
changed between these shots?* — with a Δ column, measured metrics and a profile-version diff, but
only for exactly two shots and over a chart that labels only the pressure axis. Dial-in is
cause and effect: the input you moved, then what the shot did. The page should read that way, for
every shot the user selected.

## What Changes

- **A base shot.** When the page opens, the oldest shot is the base and sits in the left
  column. Tapping any other shot makes it the base and moves it to the left; every other shot is
  compared against it. All selected shots stay visible; nothing is reduced to pairs. The base
  stays pinned on the left while the ◀ ▶ window moves through the others.
- **"What you changed"** section: only the inputs that differ from the base in some visible shot,
  with the differing cells marked and numeric inputs carrying a Δ. Inputs identical across all
  visible shots collapse into one "Unchanged" line. Includes the profile: same version, or which
  settings changed between the saved profiles.
- **A one-line summary** per shot leads the page, built by fixed rules (no AI), for example
  "Ground finer, 10 → 9.5. Ran 6.1 s longer, peaked 1.3 bar higher, and channeling went away."
- **"What happened"** section, compact by default: duration, yield (with target), first drop,
  peak pressure and mean flow, each non-base value carrying its Δ vs the base. "Show more"
  reveals peak flow, average g/s, group temperature, temperature sag, resistance, preinfusion
  and pour times, and TDS/EY. Δ colour shows direction only, never better/worse.
- **Also in "What happened"**, only when it differs between shots: why each shot stopped
  (weight, volume, profile end, by hand) and quality badges. Rating and taste taps share one
  row, and notes appear as quotes below the table.
- **Nothing-changed is stated.** A shot whose inputs all match the base says so, which separates
  "your change did nothing" from "you changed nothing" (the advisor already makes this
  distinction).
- **Grind Δ only on the same grinder and burrs**; across grinders the settings are not on one
  scale.
- **Rating**: unrated shows "—", and the row is hidden when no visible shot is rated.
- **Graph**: the base shot is drawn heavier, and a graph option lines shots up at the start of
  the pour.
- **Less scrolling**: the separate crosshair table goes away. The readout becomes a panel inside
  the graph showing only the curves that are on. Curve and phase toggles become one chip row
  under the graph, and shot visibility becomes an eye toggle on each shot's header. On wide
  windows (the landscape tablet, desktop) graph and comparison sit side by side, and only the
  comparison scrolls.
- **One-tap entry point**: "Compare with previous shot" on Shot Detail and Post-Shot Review.
  Today the only way in is multi-select in Shot History.
- **One assembler.** Input changes and metrics are computed once in C++ and consumed by the app
  page, the web `/compare/` page and the MCP `shots_compare` tool. Today four separate shot-diff
  implementations exist (`buildShotChangeDiff`, `setupChangedFromPrior`,
  `AIConversation::changesFromPreviousShot`, and the compare page's own field list); the advisor
  ones move onto the shared input diff.
- **BREAKING (MCP)**: `shots_compare` replaces its consecutive-shot `changes[]` with changes,
  metrics and Δs relative to the oldest requested shot. `McpSurfaceVersion` is bumped.
- The web `/compare/` page gets the same structure (base picker, both sections, Δ).
- Wiki manual: short entry for the compare page.

## Capabilities

### New Capabilities
- `shot-comparison`: how shots are compared — base shot selection, input-change detection,
  measured comparison metrics and their deltas, profile-version diff, and the app/web/MCP
  surfaces that present them.

### Modified Capabilities
<!-- None: advisor-user-prompt's change-detection requirement keeps its behaviour (same fields,
     same tolerances); only its implementation moves onto the shared diff. -->

## Impact

- `src/models/shotcomparisonmodel.{h,cpp}` — base shot, pinned windowing, metrics + diff per shot.
- New shared C++ unit for comparison metrics and the input diff, consumed by the model,
  ShotServer, MCP, and the advisor's diff helpers in `src/ai/dialing_blocks.cpp`,
  `src/ai/dialing_helpers.h` and `src/ai/aiconversation.cpp`.
- `src/profile/profile.{h,cpp}` `Profile::dialInDeltas` reused for shot-vs-shot profile diff;
  `ProfileManager`'s row mapping extracted so both diffs share it.
- `qml/pages/ShotComparisonPage.qml`, `qml/components/ComparisonShotTable.qml`,
  `qml/components/ComparisonGraph.qml` (base line weight, pour-start alignment, in-graph
  readout); `qml/components/ComparisonDataTable.qml` removed;
  `qml/components/ProfileDialInDiffBlock.qml` (reused for the profile row).
- `qml/pages/ShotDetailPage.qml`, `qml/pages/PostShotReviewPage.qml` — entry points.
- `src/network/shotserver_shots.cpp` `generateComparisonPage`.
- `src/mcp/mcptools_shots.cpp` `shots_compare` — new fields; bump `McpSurfaceVersion`.
- Translation keys under `comparison.*`.
- Tests: one test file for the shared diff/metrics (input tolerance, notation equivalence,
  one-sided values, profile diff, unrated rating).
- Wiki: Manual entry for Compare Shots.
