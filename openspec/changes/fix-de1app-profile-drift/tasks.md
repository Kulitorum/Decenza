## 1. Shared field-selection rule

- [x] 1.1 Add a helper that resolves de1app's four dual-spelled fields from
      `settings_profile_type` (`target_weight`, `target_volume`,
      `maximum_pressure_range_advanced`, `maximum_flow_range_advanced`), so the reader and
      `profile_sync` cannot disagree
- [x] 1.2 Unit-test the helper against both branches: `settings_2a`/`2b` take the
      plain/`_default` spelling, `settings_2c`/`2c2` take `_advanced`

## 2. Tcl reader fidelity

- [x] 2.1 Split the gate at `src/profile/profile.cpp:1348` so scalar reading is
      unconditional and only frame synthesis stays conditional on an empty `advanced_shot`
- [x] 2.2 Read the full de1app scalar set for every profile type, not only inside the
      matching `settings_2a`/`settings_2b` branch (`flow_profile_*` is currently lost on
      `2a` and `2c` profiles)
- [x] 2.3 Route `maximum_pressure_range_advanced` / `maximum_flow_range_advanced` through the
      1.1 helper instead of reading them straight from disk
- [x] 2.4 Map `profile_hide` → `hidden` on import
- [x] 2.5 Extend `tst_tclimport` with per-scalar assertions covering a `settings_2a`, a
      `settings_2b` and a `settings_2c` fixture, including one that carries both a stored
      `advanced_shot` and simple scalars
- [x] 2.6 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) — green before
      any data moves

## 3. Drift detection gate

- [x] 3.1 Add profile-level scalar comparison to `profile_sync`, using the 1.1 helper, over
      the full de1app scalar vocabulary
- [x] 3.2 Make the tool report de1app scalars that its field map does not cover, so an
      incomplete map is visible instead of silently narrowing the comparison
- [x] 3.3 Add a parity test that fails on any built-in scalar drift, naming file, field and
      both values
- [x] 3.4 Confirm the test **fails** at this point and reports the known ~338 rows — a gate
      that passes before the data is fixed is not testing anything

## 4. Built-in data re-sync

- [x] 4.1 Verify `tests/data/de1app_profiles/` still matches the current de1app checkout
      before syncing anything
- [x] 4.2 Run `profile_sync --sync` and review the diff field-by-field against the parity
      report; anything the report does not explain is a stop
- [ ] 4.3 Commit the data change on its own, with the parity test now green
- [x] 4.4 ~~Re-baseline `tests/data/profiles_legacy/`~~ — **not needed, and doing it would
      weaken the gate.** That corpus is loaded through `fromJson`, not the Tcl path, and its
      test asserts the *serializer* loses nothing. This change touches only
      `loadFromTclString`, so the corpus stayed green on its own terms. Re-baselining a
      pre-change snapshot that did not need to move would discard the very evidence it
      exists to hold.

## 5. Simple-profile frame derivation

- [x] 5.1 For `settings_2a`/`2b`, discard the stored `advanced_shot` and generate frames from
      the scalars, matching de1app's dispatch
- [x] 5.2 Add a regression test using `Steam_only` (stores 82/80/72 °C, `espresso_temperature 0`)
      asserting the generated frames follow the scalars
- [x] 5.3 Re-run 4.2–4.4 for built-ins whose frames change as a result
- [x] 5.4 Full suite green via `mcp__qtcreator__run_tests` (scope `all`)

## 6. Documentation and close-out

- [x] 6.1 Record in `docs/CLAUDE_MD/RECIPE_PROFILES.md` that `profile_sync` now compares
      profile-level scalars, and that frames remain uncompared
- [ ] 6.2 Note the simple-profile frame-derivation change in the wiki manual if the built-in
      profile behaviour is user-visible
- [x] 6.3 Open questions resolved:
      - **A-Flow built-ins re-sync from the plugin submodule** — the tool already prefers the
        plugin copy, the 5 A-Flow built-ins synced with scalar-only changes, and their
        `recipe` blocks survived (`toJsonObject()` derives them from `editorType()`, so
        nothing had to be carried over).
      - **`tests/data/de1app_profiles/` is refreshed, not pinned.** Only `Londonium.tcl` had
        drifted, by one field, and it was an upstream *fix* (de1app `84040bcd`, preinfuse
        count 0→2). Pinning would have made the new gate compare against known-stale data.
- [ ] 6.4 `openspec archive fix-de1app-profile-drift` as the last commit on the branch
