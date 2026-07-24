## 1. Canonical serializer — string encoding + non-empty steps

- [ ] 1.1 Switch `ProfileFrame::toJson` (src/profile/profileframe.cpp) to string-encode numeric fields (`temperature`, `pressure`, `flow`, `seconds`, `volume`, exit `value`, limiter `value`/`range`); keep omit-`weight`-when-zero.
- [ ] 1.2 Switch `Profile::toJson` (src/profile/profile.cpp) numeric profile-level fields to strings, matching the Visualizer builder's formatting precision.
- [ ] 1.3 Materialize simple-profile frames before emit for `settings_2a`/`settings_2b` (reuse `regenerateSimpleFrames` / the `normaliseSimpleProfile` logic) so `steps` is never empty in an emitted file.
- [ ] 1.4 Verify `Profile::fromJson` still round-trips string-encoded values (it uses `jsonToDouble`); add an explicit number-encoded-input parse test to prove dual tolerance.

## 2. Ecosystem-required and standard keys

- [ ] 2.1 Emit `tank_temperature` (= `tank_desired_water_temperature`) and `target_volume_count_start` (= `number_of_preinfuse_frames`) in `Profile::toJson`.
- [ ] 2.2 Emit `type` (derived: `settings_2a`→`pressure`, `settings_2b`→`flow`, else `advanced`), `lang`, `hidden`, `reference_file`, `changes_since_last_espresso` in `Profile::toJson`.

## 3. Unify the two writers

- [ ] 3.1 Reduce `buildVisualizerProfileJson` (src/network/visualizeruploader.cpp) to `Profile::toJson` + Visualizer-only additions; remove the duplicated per-field/per-step re-listing.
- [ ] 3.2 Reconcile the `weight`-always vs. omit-when-zero difference on the unified path in favor of omit-when-zero (D3).
- [ ] 3.3 Add a byte-parity test asserting the Visualizer upload payload is unchanged except for the intended additions.

## 4. Consumer audit (string tolerance)

- [ ] 4.1 Grep every profile-JSON numeric reader for a bare `.toDouble()` / `toInt()` / `parseFloat` on step/profile fields (`src/mcp/` profile tools, QML/JS readers).
- [ ] 4.2 Fix any non-tolerant reader to accept string-encoded values; update or regenerate golden-file test data that assumed number encoding.

## 5. Tool + built-in regeneration

- [ ] 5.1 Add a reaprime-readability checker (required keys present, `steps` non-empty, enum vocabulary for `pump`/`sensor`/`transition`/exit `type`+`condition`) reusable from both `tools/profile_sync.cpp` and a unit test.
- [ ] 5.2 Regenerate `resources/profiles/*.json` via `profile_sync --sync`; confirm the four empty-`steps` `settings_2a` files now carry explicit frames.
- [ ] 5.3 Confirm `profile_sync` compare mode reports the regenerated built-ins as functionally equal to before (format-only diff, no content change).

## 6. Tests

- [ ] 6.1 Round-trip test: parse → emit → parse yields a functionally-equal profile.
- [ ] 6.2 Reaprime-contract test over every bundled built-in (required keys, non-empty steps, enum vocabulary) — the "reaprime reads our export" gate.
- [ ] 6.3 Serialization-shape test: a sample profile emits string values, both compat aliases, and the standard metadata keys.
- [ ] 6.4 Run the full suite via Qt Creator MCP (`run_tests`, scope all) before opening the PR.

## 7. Docs

- [ ] 7.1 Update `docs/CLAUDE_MD/VISUALIZER.md` and `docs/CLAUDE_MD/RECIPE_PROFILES.md` with the canonical-format contract and the single-serializer rule.
- [ ] 7.2 Note in the docs that string encoding is the emitted form and `jsonToDouble` keeps import dual-tolerant.

## 8. Follow-on hooks (record, do not implement here)

- [ ] 8.1 Confirm `sync-builtin-profiles` (Change 2) is captured as the content-equivalence follow-on: 3-way `profile_sync` compare + `settings_2a`/stale-`advanced_shot` hardening + reaprime PRs.
- [ ] 8.2 Confirm `preserve-recipe-visualizer-roundtrip` (Change 3) carries the one-line correction that reaprime currently *rejects* (not merely strips) Decenza profiles.
