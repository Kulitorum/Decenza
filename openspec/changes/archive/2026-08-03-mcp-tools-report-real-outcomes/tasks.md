## 1. Establish the defects with failing tests first

Each of these asserts a tool reports failure for an operation that did not
happen, so each MUST be seen red against current `main` — a test written after
the fix proves only that the code does what it does. Add slots to the existing
`tests/tst_mcptools_*.cpp` files; do NOT add a test file (~1.4 s of build cost
forever per file, ~ms per slot).

- [x] 1.1 Covered as `tst_profilemanager::loadProfileReportsRefusalAndKeepsTheActiveProfile`, NOT at the MCP layer. It needs a deliberately-broken profile on disk, and the only safe place to write one is the QStandardPaths test store, which tst_profilemanager already sets up and sweeps; enabling test mode in tst_mcptools_write turned every other test in that file red (an empty profile store makes ProfileManager warn at construction, and failOnWarning does the rest). The one line in the tool that consults the return value is therefore verified by reading, not by a test — noted in the PR body
- [x] 1.2 `shots_update` with a shot ID matching no row → result carries `error`
- [x] 1.3 `shots_delete` with a shot ID matching no row → result carries `error`
- [x] 1.4 `shots_delete` when storage reports a failure → a response arrives at all (this one currently HANGS; assert with a bounded wait, and make the assertion "responded", not "responded quickly")
- [x] 1.5 The claim this originally made — that `applyPresetTheme` returning false was already covered — was FALSE; nothing tested it. Now `tst_backgroundpresets::applyPresetThemeReportsWhetherTheNameMatched`, which costs milliseconds because `settings_theme.cpp` is already linked into every test binary. The tool's one-line consumption is still untested (`mcptools_control.cpp` is in no test binary), but the decision no longer rests on a false premise
- [x] 1.6 `tst_mcptools_profiles::profileReadsReportAnUnavailableManagerAsAnError`, a `_data()` table over all four reads
- [ ] 1.7 **NOT tested**, same reason as 1.5 — `mcptools_machine.cpp` is in no test binary. The distinction between `error` (no tracker) and `hasData: false` (no sessions) is stated in the spec delta and at the call site, and is the requirement most exposed by having no test
- [x] 1.8 `shots_compare` with one bad ID among good ones → `unresolvedShotIds` names it; with all IDs bad → `error`
- [x] 1.9 Covered one level down, at the capability rather than the tool: `tst_scaleprotocol::supportsTimerMatchesWhetherTheDriverSendsAnything` asserts the per-driver flag, including Acaia's false. The tools' consumption of it (a shared `timerUnavailable()` precondition) is untested — `mcptools_scale.cpp` is in no test binary

## 2. Outcome from the operation (`void` → `bool`)

- [x] 2.1 `ProfileManager::loadProfile` (`src/controllers/profilemanager.h:339`) returns `bool`; `false` on every early return that leaves the previously active profile in place, including the unreadable-profile refusal (`profilemanager.cpp:1599-1606`)
- [x] 2.2 Do NOT add `[[nodiscard]]` — the UI callers legitimately ignore it and the annotation would force `(void)` noise at every one. Say that in a comment at the declaration so it is not "fixed" later
- [x] 2.3 `SettingsTheme::applyPresetTheme` (`settings_theme.h:224`) returns `bool`; `false` when the loop matches no preset
- [x] 2.4 `profiles_set_active` and `apply_theme` consult the return and set `error` naming the value that was rejected
- [x] 2.5 Build `all` — the compiler enumerates the QML/UI call sites; confirm each still compiles and that none of them wanted the value

## 3. Outcome from the database (rows affected, not statement ran)

- [x] 3.1 `updateShotMetadataStatic` (`shothistorystorage.h:232`) reports zero-rows-affected distinctly from a failed statement, via `query.numRowsAffected()`
- [x] 3.2 `shots_update` maps zero rows to an `error` naming the shot ID
- [x] 3.3 Same for the delete path
- [x] 3.4 Do NOT add a `SELECT` pre-check: it races the write (both tools run on a background thread) and adds a query to a one-query path. State that at the site so the "safer" version is not reintroduced

## 4. `shots_delete` answers on every terminal outcome

- [x] 4.1 Establish which signals can terminate `requestDeleteShot`. The tool connects `shotDeleted` only; the failure path emits `errorOccurred`
- [x] 4.2 Done as preferred: `shotDeleteFinished(shotId, success, reason)`. `errorOccurred` was not used, so 4.3 does not apply
- [x] 4.3 N/A — the delete-specific signal was not invasive. (Original: if it proves invasive, connect `errorOccurred` as the fallback and document the imprecision AT the connection, not in the change docs where the next reader will not see it
- [x] 4.4 Disconnect both connections from whichever fires first; respond exactly once. A double `respond()` on the `_deferred` path writes twice to the socket
- [x] 4.5 Do NOT add a generic `_deferred` watchdog — one known instance, and a timeout for a hazard with one instance is machinery ahead of evidence (design non-goal)

## 5. Unavailable dependency reports an error

- [x] 5.1 The four `mcptools_profiles.cpp` reads that `return result;` on a null `ProfileManager` get `result["error"] = "Profile manager not available";` — the exact string the sibling guards in that same file already use. Grep the file for the existing wording rather than inventing a new one
- [x] 5.2 `steam_get_health` reports an unavailable tracker as `error`
- [x] 5.3 `hasData: false` keeps meaning "no steam sessions yet". Add a test asserting the two states differ (1.7) — this is the requirement most likely to be collapsed by a later tidy-up
- [x] 5.4 Empty-string enum values are a convention violation on their own (`MCP_SERVER.md` data conventions); confirm no other tool returns `status: ""`

## 6. Dropped inputs, no-ops, and device capability

- [x] 6.1 `shots_compare` gains `unresolvedShotIds`; all-unresolved becomes an `error`
- [x] 6.2 `devices_connect_de1` reports the already-connected state in a machine-readable field and still echoes the address the caller asked for (today it discards it)
- [x] 6.3 `mqtt_disconnect` classifies "not connected" the same way `mqtt_publish_discovery` does. Pick ONE classification for that state and apply it to both, rather than adding a third
- [x] 6.4 `ScaleDevice` gains `virtual bool supportsTimer() const { return false; }` (`src/ble/scaledevice.h:55-57`), overridden `true` by every driver that implements the three timer methods
- [x] 6.5 Enumerated all 16 ScaleDevice subclasses. 10 send real commands and override `true`; Acaia overrides **false** — it declares all three slots as EMPTY bodies with a comment saying it has no remote timer control, so the override list alone gets it wrong. FlowScale, Hiroia, SmartChef, VariaAku and SimulatedScale inherit the false default
- [x] 6.6 The three timer tools consult it and return an `error` naming the scale when unsupported
- [x] 6.7 Do NOT touch `hasResetTimerSideEffects` (`scaledevice.h:58-61`) — a different question about the same methods

## 7. Do not sweep up the deliberate partial outcomes

- [x] 7.1 Leave `set_flow_calibration`'s `warning`, `profiles_edit_params`' `ignoredFields`, and `dialing_get_grinder_calibration`'s `available: false` exactly as they are. Each is a reviewed decision with a comment; they are the reason this change is scoped by defect rather than by pattern
- [x] 7.2 Leave `settings_set` alone. Its ~90 `void` setters and its build-the-response-first ordering are a real mechanism with no identified failing key; a 90-setter refactor on a hypothetical is over-build (design non-goal, recorded under Open Questions)

## 8. Verify

- [x] 8.1 Full suite via `mcp__qtcreator__run_tests` (scope `all`) — ask first, Qt Creator is shared
- [x] 8.2 Break each fix in turn and confirm its section-1 test goes red
- [x] 8.3 Done, over BOTH entry paths. ShotServer (`localhost:8888/mcp`) by raw HTTP: `shots_update`/`shots_delete` on a nonexistent id, `apply_theme` with an unknown name, `shots_compare` with mixed and all-bad ids, `steam_get_health`, and all three `scale_timer_*` against a Simulated Scale — every failure carried `isError: true`, and `machine_get_state` carried no `isError` key at all. `McpRemoteAccess` (the claude.ai remote connector) separately confirmed to deliver `isError` for `shots_update`, which raw curl could not reach
- [ ] 8.4 Read the `text-invariants.yml` PR run — it gates `src/**` and nothing blocks a merge on it

## 9. Document

- [x] 9.1 `docs/CLAUDE_MD/MCP_SERVER.md`: the outcome convention next to the `error`-key convention #1754 added — success means it happened, unavailability is an error, dropped inputs are named
- [ ] 9.2 Name every behaviour change in the PR body: calls that used to report success and now report failure, one line each
- [x] 9.3 No wiki manual change — MCP tools are not a user-visible app surface
- [ ] 9.4 Archive the change + spec sync as the final commit on the same PR
