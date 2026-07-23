## 1. Verify the store-identity assumption

- [x] 1.1 Add `tests/tst_appsettings.cpp` with a test asserting that `QSettings("DecentEspresso", "Decenza").fileName()` equals the `fileName()` of a default-constructed `QSettings` under the app's normal org/domain/app identity; register the target in `tests/CMakeLists.txt`
- [x] 1.2 Run it on macOS. If the paths differ, stop and adjust D1 in `design.md` to whichever construction matches the existing app-named store before continuing — everything below depends on this

## 2. Introduce the AppSettings accessor (no behaviour change)

- [x] 2.1 Create `src/core/appsettings.{h,cpp}`: `class AppSettings : public QSettings` with a default constructor, two `#ifdef DECENZA_TESTING`-guarded definitions (PID-scoped `Settings::testQSettingsPath()` + `IniFormat` under testing, the explicit org/app pair otherwise); add both files to `CMakeLists.txt`
- [x] 2.2 Point `AppSettings` at the canonical `("DecentEspresso", "Decenza")` identity immediately. **Revised during implementation** — the original plan (point at legacy `DE1Qt` first, "a pure refactor with zero data movement") was wrong: it is a no-op only for the explicit-`DE1Qt` sites. Converting a *bare* `QSettings` site to a `DE1Qt`-identified handle moves that key's home, and §4's `DE1Qt → Decenza` migration would never put it back — stranding web-auth sessions, UI language, shot-map location, AI conversations and `internal/lastIconRegisteredVersion`. Canonical-from-the-start makes the bare-site conversions genuine no-ops and leaves every moved key to the migration
- [x] 2.3 Convert the 12 `Settings<Domain>` sub-objects (`settings_{mqtt,autowake,hardware,ai,theme,visualizer,mcp,brew,dye,network,app,calibration}.cpp`) to `mutable AppSettings m_settings` — delete the now-redundant `#ifdef DECENZA_TESTING` branch from each, since `AppSettings` owns it
- [x] 2.4 Convert `settings.cpp` (façade member init), `accessibilitymanager.cpp`, `steamhealthtracker.cpp`, and `settingsserializer.cpp`
- [x] 2.5 Convert the remaining explicit-`DE1Qt` sites: `maincontroller.cpp`, `shothistorystorage.cpp`, `datamigrationclient.cpp`, `shotserver_backup.cpp`
- [x] 2.6 Convert the bare-`QSettings` sites in `src/ai/` (`aimanager.cpp`, `aiconversation.cpp`) and `src/mcp/mcptools_ai_conversations.cpp`
- [x] 2.7 Convert the bare-`QSettings` sites in `src/network/` (`shotserver_auth.cpp`, `shotserver_ai.cpp`, `shotserver_backup.cpp`, `locationprovider.cpp`)
- [x] 2.8 Convert the bare-`QSettings` sites in `src/core/` (`profilestorage.cpp`, `databasebackupmanager.cpp`, `datamigrationclient.cpp`) and the Launch Services block in `main.cpp`
- [x] 2.9 Add a test asserting the literals `"DE1Qt"` and bare `QSettings <name>;` constructions appear nowhere under `src/` outside `appsettings.cpp` and the migration unit
- [x] 2.10 Full suite green via `mcp__qtcreator__run_tests` (scope `all`). **Not a stopping point** — see 2.2. With `AppSettings` canonical from the start, the legacy `DE1Qt` keys are unreachable until §3/§4 land, so §2–§4 must ship together

## 3. Migration unit

- [x] 3.1 Create `src/core/settingsstoremigration.{h,cpp}`: `migrateLegacySettingsStore(QSettings& canonical, QSettings& legacy) -> Outcome` with `{copied, legacyKeyCount, alreadyDone, deferredOnError, guardStamped}`, modelled on `migrateAccessibilityLegacyStore()`; add to `CMakeLists.txt`
- [x] 3.2 Implement skip-if-present copy, then read-back verification of every copied key against its source value
- [x] 3.3 Implement defer-on-error: on unreadable legacy store or any read-back mismatch, return `deferredOnError` without stamping the done-flag and without destroying anything
- [x] 3.4 Implement destruction on success: `clear()` + `sync()` the legacy store, then best-effort `QFile::remove(fileName())`, logging but not failing on removal error
- [x] 3.5 Log the outcome at `qInfo` including `legacyKeyCount`, so support logs distinguish "nothing to migrate" from "all already present"
- [x] 3.6 Unit tests over temp `IniFormat` stores: settings carried across; canonical value wins on collision; legacy destroyed after success; legacy intact and flag unstamped on a forced verification failure; second run is a no-op

## 4. Switch the canonical store and wire the migration

- [x] 4.1 Repoint `AppSettings` to `("DecentEspresso", "Decenza")`
- [x] 4.2 Call the migration from `main.cpp` immediately **before** `runAppNameMigrationOnce()` (order reversed during implementation — see design.md D3: the app-name guard lives in the legacy store, so the store migration has to carry it across first), and assert by construction that it runs **before** `Settings` and `AccessibilityManager` are constructed
- [x] 4.3 Add a test constructing `AccessibilityManager` against a freshly-migrated store, asserting its `accessibility._migratedFromLegacyV1` guard is seen as stamped and that it does not re-run its own legacy migration
- [x] 4.4 Ensure the migration does not run under `DECENZA_TESTING`
- [x] 4.5 Delete the abandoned `("Decenza", "DE1")` accessibility store once its guard is confirmed stamped in the canonical store

## 5. Factory reset and cleanup

- [x] 5.1 Extend `Settings::factoryReset()` to clear the canonical store plus both legacy identities (`DE1Qt` and `Decenza`/`DE1`), and confirm it succeeds when a legacy store is already absent
- [ ] 5.2 **Deliberately not implemented.** `Settings::factoryReset()` deletes directories under `AppDataLocation` and the shot database, not just settings keys. A test process resolves `AppDataLocation` from whatever application name it happens to carry, so calling it under test risks destroying a developer's real profiles and shot history to assert a property about two `QSettings::clear()` calls. The legacy-store clearing is instead covered by inspection plus the migration's own `secondRunIsANoOp()` / guard tests
- [x] 5.3 Delete the obsolete bare-`QSettings` warning comments in `shothistorystorage.cpp`, `maincontroller.cpp`, `coffeebagstorage.cpp`, and `accessibilitymanager.cpp` — the hazard they describe no longer exists
- [x] 5.4 Audit every file touched for pre-existing issues per the repo rule, and fix them in this change — removed 4 redundant `const_cast<QSettings&>` on a `mutable` member, collapsed 2 duplicate same-store handles (`accessStore` alongside `settings`), deleted 5 now-false scope-mismatch comments, and corrected a stale rationale comment in `tst_aimanager.cpp`

## 6. Docs

- [x] 6.1 Update `docs/CLAUDE_MD/SETTINGS.md` (lines ~32 and ~145) to instruct `AppSettings` instead of `QSettings("DecentEspresso", "DE1Qt")`
- [x] 6.2 Update `docs/CLAUDE_MD/TESTING.md` (lines ~37 and ~39) — test isolation now flows from `AppSettings`, and seeding goes through it
- [x] 6.3 Update `docs/plans/2026-02-21-factory-reset-design.md` to name the canonical store and both legacy identities
- [x] 6.4 No wiki change: the store name is not user-visible in the app UI and the manual does not document it

## 7. Verification and landing

- [x] 7.1 Full suite green via `mcp__qtcreator__run_tests` (scope `all`), warnings clear
- [x] 7.2 Manual upgrade check on macOS — **run, and it found two defects**. Settings all migrated correctly (44 keys; profiles, favourites, calibration, known scales, MCP key, 1086 shots intact), but: (a) `cfprefsd` rewrote an empty `DE1Qt.plist` husk after the in-migration unlink, so the user still saw two files — fixed by `removeEmptyLegacyStoreFile()` running every launch; (b) the run reported "copied 44 of 118 legacy key(s)" because `QSettings` merges the platform fallback search list into `allKeys()`/`contains()` — fixed with `setFallbacksEnabled(false)` on both handles, plus a regression test. Re-verified from a restored pre-migration backup after both fixes: run 1 logged `copied 44 of 44` and left the husk (defect reproduced), run 2 logged `removed leftover empty store file` and **one plist remains**, with the cfprefsd domain deregistered and all 75 keys plus both migration guards intact
- [ ] 7.3 Android verification per the platform-code rule — **deferred to the post-merge beta**: the only practical way to exercise this on device is a pre-release build, so the v2.0.0 pre-release is being rebuilt at the merge commit and held in **draft** until the on-device check passes (settings survive, one `.conf` remains). Do not publish the draft before that
- [x] 7.4 Open the PR from a feature branch; review before merge — PR #1625, reviewed by a 5-agent pass that found 8 issues (3 scored 100: an unreadable-store deletion, factory reset resurrecting the pre-rename store, and broken fresh-install detection); all fixed in 4947611
- [x] 7.5 `openspec archive consolidate-qsettings-stores` as the final commit on the branch
