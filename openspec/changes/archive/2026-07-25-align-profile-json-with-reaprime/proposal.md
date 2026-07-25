## Why

The Decent community needs a profile to produce the **same espresso in every app** — a profile authored or shared in one DE1 app must run identically after import into another. That guarantee has two halves: the file must be *readable* everywhere (format), and its content must be *equivalent* everywhere (sync). This change delivers the first half; content sync (Change 2, below) delivers the second.

reaprime (the Flutter DE1 app that, alongside de1app, defines the community "v2" profile format) **cannot read a Decenza-exported profile**. Its `Profile.fromJson` hard-rejects any profile missing `tank_temperature` or `target_volume_count_start`, or with an empty `steps` array — and Decenza emits none of the first two and ships four built-ins with empty steps. Separately, Decenza serializes profiles through **two independently hand-maintained writers** (`Profile::toJson` and `buildVisualizerProfileJson`) that have already drifted: the Visualizer path emits the compat keys and string-encoded values, the on-disk/export path does not. Aligning the format is the foundation for syncing built-in profiles and for the Visualizer recipe round-trip, so it goes first.

## What Changes

- **One canonical serializer.** `Profile::toJson` becomes the single source of truth for Decenza's profile JSON; `buildVisualizerProfileJson` delegates to it and layers only Visualizer-specific additions. This stops the two writers from drifting again (a repeat failure mode — see the standing `buildHistoryShotJson` drift warning in `docs/CLAUDE_MD/VISUALIZER.md`).
- **Canonical string encoding.** Numeric step and profile fields serialize as strings (`"9.0"`, not `9.0`), matching de1app / the tablet / Visualizer / reaprime's own output. reaprime already *reads* numbers, but writing strings makes a Decenza file byte-shaped like every other DE1 app and safe against any strict parser in the ecosystem. **BREAKING** for byte-level consumers/golden tests (semantically compatible: our own `jsonToDouble` reads both).
- **Emit the ecosystem-required and standard keys.** `tank_temperature` (alias of `tank_desired_water_temperature`) and `target_volume_count_start` (alias of `number_of_preinfuse_frames`) — both hard-required by reaprime — plus the tablet metadata `type`, `lang`, `hidden`, `reference_file`, `changes_since_last_espresso` that the Visualizer path already emits.
- **Never emit an empty `steps` array.** Simple `settings_2a`/`settings_2b` profiles (which ship with `steps: []` and regenerate frames at activation) are expanded to explicit frames in any file Decenza emits, so even a raw exported/bundled file is a valid, runnable profile in any app. Preserve omit-`weight`-when-zero (matches reaprime's `parseOptionalDouble`, which reads absent as null).
- **Leverage and update `tools/profile_sync.cpp`.** Its output flows through `Profile::toJson`, so it emits the new format for free once the serializer changes; re-run it (`--sync`) to regenerate all built-ins in reaprime-readable form. Add a **reaprime-readability lint** (required keys present, `steps` non-empty, enum values valid) as an acceptance gate so a future built-in can't regress.
- **Audit internal consumers for string tolerance.** Every reader of profile JSON numeric fields (MCP profile tools, any QML/JS reader, golden-file tests) must tolerate string-encoded values. `Profile::fromJson` already does via `jsonToDouble`; the rest need a pass.
- **NOT doing (recorded so it isn't lost):**
  - **Change 2 — `sync-builtin-profiles`** (separate, follow-on): reconcile the *content* of the ~63 common built-ins with reaprime, case-by-case (no blanket authority rule), driven by an extended 3-way `profile_sync` (de1app-Tcl ↔ Decenza-JSON ↔ reaprime-JSON) plus a `settings_2a` hardening that prefers settings-derived frames over a stale `advanced_shot` (the lever-corruption lesson from reaprime's own curation pass). Produces Decenza content fixes + a user migration **and** upstream PRs to reaprime where Decenza is the more faithful side (e.g. A-Flow 9-frame).
  - **Change 3 — `preserve-recipe-visualizer-roundtrip`** (existing change, follow-on): carry the high-level `recipe` block through a Visualizer upload→download round-trip. Builds on this change's aligned format. Its `design.md` should get a one-line correction: reaprime currently *rejects* Decenza profiles (not merely passthrough-strips unknown keys) — this change is what makes the "passthrough" framing true.
  - Content reconciliation, dedup, and A-Flow/D-Flow value differences are **out of scope here** — this change only makes the built-ins *parseable* by reaprime, not *equivalent* in content.

## Capabilities

### New Capabilities
- `profile-json-interchange`: The canonical serialization contract for Decenza profile JSON — single-serializer rule, string encoding, the required/standard key set, and the non-empty-`steps` guarantee — such that any profile Decenza emits is readable by reaprime and other DE1 v2 apps without loss.

### Modified Capabilities
<!-- None. visualizer-upload-persistence governs id write-back, not payload shape; its requirements are unchanged. The upload payload merely delegates to the new canonical serializer. -->

## Impact

- **Serializer:** `src/profile/profileframe.cpp` (`ProfileFrame::toJson` → strings) and `src/profile/profile.cpp` (`Profile::toJson` → strings, compat/standard keys, expand simple-profile steps).
- **Visualizer upload:** `src/network/visualizeruploader.cpp` (`buildVisualizerProfileJson` delegates to `Profile::toJson`; Visualizer-only extras layered on top). Guarded by a byte-parity test so the upload payload is provably unchanged except for intended additions.
- **Dev tool:** `tools/profile_sync.cpp` — regenerate built-ins in the new format; add reaprime-readability lint.
- **Bundled data:** `resources/profiles/*.json` — regenerated; the four empty-`steps` `settings_2a` files gain explicit frames.
- **Consumer audit:** `src/mcp/` profile tools, any QML/JS profile reader, golden-file tests — confirm string tolerance.
- **Tests:** parse→emit→parse round-trip, Visualizer-payload byte-parity, and a "reaprime reads our export" fixture (reaprime's required-key + non-empty-steps contract).
- **Docs:** `docs/CLAUDE_MD/VISUALIZER.md` and `docs/CLAUDE_MD/RECIPE_PROFILES.md` — document the canonical format and single-serializer rule.
- **No persisted-data migration** in this change (encoding is backward-compatible on read). Content migration belongs to Change 2.
