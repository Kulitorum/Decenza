## Context

**North star:** the Decent community needs a profile to make the same coffee in every app — import a profile into any DE1 app and get the identical extraction. That requires files that are *readable* everywhere (format) and content that is *equivalent* everywhere (sync). This change is the format half; `sync-builtin-profiles` (Change 2) is the content half.

Today Decenza has **three profile serializations that have drifted**:

| Path | Encoding | `tank_temperature` | `target_volume_count_start` | reaprime-readable |
|---|---|---|---|---|
| `Profile::toJson` (profile.cpp:364) — on-disk, export, share-code | numbers | ✗ (`tank_desired_water_temperature`) | ✗ (`number_of_preinfuse_frames`) | **✗ rejected** |
| `buildVisualizerProfileJson` (visualizeruploader.cpp:1121) — upload | strings | ✓ aliased | ✓ aliased | ✓ |
| Bundled `resources/profiles/*.json` | numbers | ✗ | ✗ | ✗ (+4 empty-`steps`) |

reaprime's `Profile.fromJson` (`lib/src/models/data/profile.dart`) throws `ArgumentError` when `tank_temperature` or `target_volume_count_start` is absent, or when `steps` is empty; its `parseDouble` accepts both string- and number-encoded values. So the Visualizer builder — which already emits string values, both aliases, and the tablet metadata (`type`/`lang`/`hidden`/`reference_file`/`changes_since_last_espresso`) — is effectively the reaprime-faithful reference implementation. The general serializer simply never received those additions. **The drift is the bug.** `docs/CLAUDE_MD/VISUALIZER.md` already carries a standing warning that the live vs. history upload builders drifted the same way; this is a recurring failure mode in this code.

Decenza's own re-parse is safe either way: `Profile::fromJson` reads values through `jsonToDouble` (profileframe.cpp:6), which parses strings and numbers.

## Goals / Non-Goals

**Goals:**
- Any profile Decenza emits is readable by reaprime (and any strict DE1 v2 parser) without loss.
- A single canonical serializer; the Visualizer payload derives from it.
- Canonical string encoding matching the rest of the ecosystem.
- Every emitted file has a non-empty, runnable `steps` array — including simple `settings_2a/2b` profiles and bundled files.
- The `profile_sync` dev tool regenerates the built-ins in the new format and lints them against reaprime's contract.

**Non-Goals:**
- Content reconciliation / dedup / A-Flow–D-Flow value differences (Change 2). This change makes files *parseable*, not *equivalent*.
- The `recipe` round-trip through Visualizer (Change 3).
- Any persisted-data migration. Encoding is backward-compatible on read; content migration belongs to Change 2.

## Decisions

### D1. Unify: `Profile::toJson` is the single source of truth; the Visualizer builder delegates
The two writers independently re-walk the steps and re-list every key, which is exactly why they drifted. Make `Profile::toJson` the canonical serializer and reduce `buildVisualizerProfileJson` to `Profile::toJson` + Visualizer-only extras (multipart wrapping stays in the caller; the `recipe` block is Change 3). The string decision (D2) collapses the biggest difference between the two, which is what makes delegation low-risk now rather than a rewrite.

*Alternative — parity only (add the keys to `Profile::toJson`, leave two functions):* rejected. It fixes this instance but leaves two sources of truth, so the next added key drifts again.

*Guard:* a byte-parity test asserts the Visualizer upload payload is unchanged except for the intended additions, so delegation provably doesn't regress the tuned upload path.

### D2. Canonical string encoding
Emit numeric fields as strings in both `ProfileFrame::toJson` and `Profile::toJson`. reaprime reads numbers, but de1app / the tablet / Visualizer / reaprime all *write* strings, and matching them makes a Decenza file byte-shaped like every other app and robust to any strict parser. Import stays dual-tolerant via `jsonToDouble`.

*Alternative — keep numbers:* lower churn and reaprime still reads them, but it leaves Decenza the lone number-encoder and risks a strict third-party parser. Rejected in favor of ecosystem uniformity.

### D3. Reconcile the one behavioral difference between the writers deliberately
`buildVisualizerProfileJson` emits `weight` always; `Profile::toJson` emits it only when `exitWeight > 0`. Keep the **omit-when-zero** behavior in the unified path — it matches reaprime's `parseOptionalDouble` (absent → null) and is the correct semantic. This is a conscious reconciliation, not an accident to preserve.

### D4. Materialize simple-profile frames before emit
`settings_2a/2b` profiles ship with `steps: []` and regenerate frames at activation (`regenerateSimpleFrames`). Any emit path must first materialize frames so the file has non-empty steps. `profile_sync` already has `normaliseSimpleProfile` doing exactly this for comparison; reuse that logic on the emit side. This makes even a raw bundled file a valid profile in any app.

### D5. `profile_sync` is the regeneration + lint engine
Its output already flows through `Profile::toJson`, so it emits the new format for free once the serializer changes. Re-run `--sync` to rewrite the built-ins. Add a reaprime-readability lint (required keys, non-empty steps, enum vocabulary) usable both in the tool and as a test, so a future built-in can't silently regress. (The 3-way de1app↔Decenza↔reaprime compare and the `settings_2a`-vs-stale-`advanced_shot` hardening are Change 2, not here.)

### D6. Consumer audit for string tolerance
Switching on-disk to strings changes the bytes of every profile Decenza writes, including user profiles on next re-save. `Profile::fromJson` is safe (`jsonToDouble`), but any other reader that does a bare `.toDouble()` / `parseFloat` on these fields would read 0. Audit `src/mcp/` profile tools, QML/JS profile readers, and golden-file tests; fix or update as found. This is a task, not a hope.

## Risks / Trade-offs

- **String encoding changes the bytes of every emitted profile, incl. existing saved user profiles on re-save** → backward-compatible on read (`jsonToDouble`); covered by the consumer audit (D6) and round-trip tests. It is a visible diff in exported files and golden data — expected, not a regression.
- **Delegation could regress the tuned Visualizer upload** → byte-parity test on the upload payload (D1) is the gate; any diff beyond the intended additions fails the build.
- **A hidden consumer assumes numbers** → the D6 audit; the readability lint and round-trip tests catch the profile side, but a non-profile reader (e.g. an MCP field) needs the explicit grep pass.
- **Regenerating all built-ins is a large diff** → deterministic (tool-produced), reviewable as "format only"; content is asserted unchanged by `Profile::functionallyEqual` before/after in `profile_sync` compare mode.
- **Scope creep into content sync** → hard line: this change only makes files parseable. Equivalence is Change 2, explicitly.

## Migration Plan

1. Switch `ProfileFrame::toJson` and `Profile::toJson` to strings + compat/standard keys + simple-frame materialization (D2, D4).
2. Delegate `buildVisualizerProfileJson` to `Profile::toJson`; add the byte-parity test (D1).
3. Run the consumer audit; fix any non-tolerant reader (D6).
4. Add the reaprime-readability lint; regenerate built-ins via `profile_sync --sync` (D5).
5. Update `VISUALIZER.md` / `RECIPE_PROFILES.md` with the canonical-format + single-serializer rule.

Rollback is code-only (no persisted-data migration): revert the serializer and regenerated built-ins.

## Open Questions

- Exact home for the reaprime-readability lint: a mode inside `profile_sync` vs. a standalone unit test over `resources/profiles/`. Leaning toward both — a reusable checker called from a test.
- Whether to also preserve unknown top-level keys through `Profile::fromJson`/`toJson` for passthrough friendliness (mirrors the reaprime concern in Change 3) — likely defer to Change 3.
- Confirm no CI/release step consumes the bundled profile JSON expecting number encoding before regenerating.

## Follow-on changes (recorded so they are not lost)

- **Change 2 — `sync-builtin-profiles`** (Decenza + reaprime PRs): content reconciliation of the ~63 common built-ins, case-by-case (no blanket authority rule). Extend `profile_sync` to a 3-way compare (de1app-Tcl ↔ Decenza-JSON ↔ reaprime-JSON); harden the `settings_2a` path to prefer settings-derived frames over a stale `advanced_shot` (the lever-corruption lesson from reaprime's #242 curation). Ships Decenza content fixes + a user migration and upstream PRs where Decenza is the more faithful side (e.g. A-Flow 9-frame, which `profile_sync`'s plugin-canonical override already gets right).
- **Change 3 — `preserve-recipe-visualizer-roundtrip`** (existing, 4 repos): carry the high-level `recipe` block through a Visualizer upload→download round-trip, building on this change's aligned format. Add a one-line correction to its `design.md`: reaprime currently *rejects* Decenza profiles (missing `tank_temperature`), not merely passthrough-strips unknown keys — this change is what makes the "passthrough" framing true.
