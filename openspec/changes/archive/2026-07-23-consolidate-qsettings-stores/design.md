## Context

Decenza persists preferences into two independent `QSettings` stores. Roughly 34 call sites open
an explicit `QSettings("DecentEspresso", "DE1Qt")` — including all 12 `Settings<Domain>`
sub-objects, `AccessibilityManager`, and `SteamHealthTracker` — while roughly 37 sites
default-construct `QSettings`, which resolves through the org/app identity set in `main.cpp` to a
second store named after the app. On macOS these are `com.decentespresso.DE1Qt.plist` and
`com.decentespresso.Decenza.plist`; the same split exists as two registry keys on Windows and two
`.conf` files on Linux and Android. On the reference installation the two stores hold 38 and 30
keys with **zero key overlap**.

A third store, `("Decenza", "DE1")`, was created by an early `AccessibilityManager` and has
already been migrated away by `migrateAccessibilityLegacyStore()`; it survives only as a husk that
`factoryReset()` wipes explicitly. That episode is the precedent for this one, and its
implementation — a pure function over two supplied handles, returning an outcome record, with
defer-on-error semantics — is the pattern this design reuses.

Constraints: settings are read on background threads (`ShotHistoryStorage`, `CoffeeBagStorage`), so
any shared handle would be a data race; `Settings::testQSettingsPath()` must keep isolating tests;
and `main.cpp` mutates `QCoreApplication::applicationName()` during the existing
`Decenza DE1` → `Decenza` app-name migration, which makes any default-constructed handle
constructed during that window address the wrong store.

## Goals / Non-Goals

**Goals:**
- Exactly one settings store per platform, named after the shipped app.
- The store identity named in exactly one place in the codebase.
- Existing user settings survive the upgrade with byte-identical key strings.
- Test isolation covers every consumer, including those that bypass it today.
- The legacy store is gone from disk afterwards — not merely unused.

**Non-Goals:**
- Changing any key string, key layout, or settings semantics.
- Downgrade support. A build predating this change finds no legacy store and starts from
  defaults. Accepted deliberately: it is the price of deleting the legacy file, and deleting the
  file is the visible payoff.
- Touching QML. `Settings.<domain>.<prop>` access is unaffected.
- Reworking `Settings`' domain decomposition, or the settings serializer's export format.

## Decisions

### D1 — Canonical store is `("DecentEspresso", "Decenza")`

**Alternatives considered.** *Keep `DE1Qt`*: the smaller migration in raw terms and the safer
rollback story, but it preserves a store named after an app Decenza has never shipped as — the
exact confusion that prompted this change — and every future contributor and support case meets it
again. *Keep both stores, documented*: zero migration risk, but leaves the footgun that has
already produced four warning comments and one isolated-store incident. *Chosen*: the app-named
store, because the store name is user-visible on every platform and a name nobody recognises is
itself the defect.

The two-argument form is used rather than relying on the default constructor, so the identity
does not depend on mutable `QCoreApplication` state (see Context). Evidence from the reference
installation says the explicit form and the default form resolve to the same backing file: the
explicit `("DecentEspresso", "DE1Qt")` pair produced `com.decentespresso.DE1Qt.plist`, so
`("DecentEspresso", "Decenza")` yields `com.decentespresso.Decenza.plist` — the file the default
form already writes. **This is inferred, not yet observed**, and task 1 verifies it with an
assertion before anything else is built on it.

### D2 — A `QSettings` subclass, not a factory function

`QSettings` is a `QObject` and therefore non-copyable, so an `appSettings()` factory cannot return
one by value; returning a reference to a shared static would be a data race given background-thread
access. Instead:

```cpp
class AppSettings : public QSettings {
public:
    AppSettings();   // canonical identity, or the PID-scoped test store under DECENZA_TESTING
};
```

Every call site changes one token — `QSettings settings;` becomes `AppSettings settings;`, and
`mutable QSettings m_settings("DecentEspresso", "DE1Qt")` becomes `mutable AppSettings m_settings`.
Each site keeps its own handle exactly as today, so threading behaviour is unchanged. The identity
and the `DECENZA_TESTING` branch live in one constructor, expressed as two `#ifdef`-guarded
definitions.

A useful consequence: after this change the default-constructed `QSettings()` and `AppSettings`
address the *same* store. A call site accidentally missed during conversion still reads and writes
the right data — it merely bypasses test isolation. Only a missed **explicit `DE1Qt`** site is
genuinely broken, and those are the ones a grep can find exhaustively.

### D3 — Migration is a pure function, run before any settings object exists

`migrateLegacySettingsStore(QSettings& canonical, QSettings& legacy) -> Outcome` lives in
`src/core/settingsstoremigration.{h,cpp}`, mirroring `migrateAccessibilityLegacyStore()`. It takes
both handles as parameters so tests drive it with temp `IniFormat` files and never touch a real
store. `main.cpp` calls it alongside `runAppNameMigrationOnce()`.

**Ordering is load-bearing**, and one edge is subtle:

1. Legacy-store migration (`DE1Qt` → `Decenza`) runs **first**. **Reversed during
   implementation** — this section originally specified the opposite order. The app-name
   migration's own done-flag lived in the legacy `DE1Qt` store, and this migration is what
   carries it into the canonical store; run the app-name migration first and it sees an
   unstamped flag and redundantly re-runs on every already-migrated installation.
2. Existing app-name migration (`Decenza DE1` → `Decenza`) runs second, reading its done-flag
   from the canonical store.
3. **The legacy-store cleanups run on every launch, not only the migrating one.** Found in
   review: gating them behind the already-migrated early return stranded the abandoned
   `("Decenza", "DE1")` store forever for anyone whose accessibility guard gets stamped *later in
   the same launch* — i.e. users upgrading from a build predating that migration. They are
   idempotent, so running them unconditionally costs nothing and eventually collects every store.
   They are skipped only on the defer path, where the migration deliberately preserved a store it
   could not read.
4. **Both must complete before `AccessibilityManager` is constructed.** Its
   `migrateLegacyStore()` guard flag (`accessibility._migratedFromLegacyV1`) currently lives in
   the `DE1Qt` store. If the accessibility manager is constructed before that flag has been copied
   across, it sees an unstamped guard and re-runs its own legacy migration, overwriting the user's
   *current* accessibility settings with values from the abandoned `("Decenza", "DE1")` store.
   That is a real regression, not a theoretical one, and ordering is the whole mitigation.

Skip-if-present means the canonical store wins on collision. With zero observed overlap this is
close to moot, but it is the safe direction: a value already in the canonical store is one the
running build wrote.

**One documented exception, found in review.** That premise fails for keys the pre-consolidation
code wrote to *both* stores from different places, because then the canonical copy is the output of
the store-scope bug this change removes. `localization/language` is the known instance:
`TranslationManager` read and wrote it through `Settings` (the legacy store), while the
backup-restore and device-to-device paths wrote it to the default — now canonical — store with a
bare `QSettings`. A user who set a language and later restored a backup has the live value in the
legacy store and a stale one in the canonical store, so plain skip-if-present would silently change
their UI language on upgrade. The migration therefore carries a small legacy-wins list.

### D4 — Verify, then destroy

The legacy store is destroyed only after every copied key is read back from the canonical store and
compared to its source value. On any mismatch, or if the legacy store cannot be read, the function
returns `deferredOnError` **without** stamping the done-flag, leaving the legacy store fully
intact so the next launch retries. This is `migrateAccessibilityLegacyStore()`'s existing
defer-on-error contract, which matters more here because this migration deletes its source.

Destruction is `clear()` + `sync()` first — authoritative through the API — and only then a
best-effort `QFile::remove(fileName())`. Failure to remove the file is logged, not fatal: the keys
are already gone, so correctness does not depend on the unlink.

**Corrected after the first real run.** This section originally claimed that clearing through
`QSettings` first was enough to stop `cfprefsd` resurrecting the file. It is not: on macOS the
daemon owns the domain and writes its now-empty copy back to disk *after* the unlink, leaving a
42-byte husk — so the user still saw two preference files, which is the exact failure this change
exists to prevent. The unlink cannot win that race from inside the migration.

The fix is not to fight the daemon but to pick a later event: `removeEmptyLegacyStoreFile()` runs
on **every** launch, removing a legacy store's file if the store holds no keys. By the next launch
`cfprefsd` has flushed and dropped the domain — verified: `defaults delete` reported the domain
already absent while the file remained — so the unlink sticks and is thereafter a no-op. No timer,
no retry loop; the next launch is the event, per the project's rule against timers as guards.

**Both handles must also have fallbacks disabled.** `QSettings::allKeys()` and `contains()` consult
the platform search list, so on macOS the first real run enumerated 118 "legacy" keys for a store
holding 44 — the extra 74 were system preferences reached through org-level and global domains.
They were skipped only because `contains()` consulted the same fallbacks on the canonical side;
that symmetry is not something to rely on, and without `setFallbacksEnabled(false)` the migration
can copy foreign preferences into Decenza's store.

### D5 — Fold in the third store

Since the goal is "one store", the abandoned `("Decenza", "DE1")` accessibility store is deleted
in the same change, but only once its own guard flag is confirmed stamped in the canonical store.
`factoryReset()` keeps clearing both legacy identities: the reset must remain safe on an
installation where a reset happens *before* the migration ever runs, since the done-flags live in
the store the reset erases.

## Risks / Trade-offs

- **The `("DecentEspresso", "Decenza")` pair might not resolve to the default-constructed store's
  file** → Task 1 asserts `fileName()` equality between the two forms on every desktop platform
  before any conversion work proceeds. If they differ, the accessor uses whatever form matches the
  existing app-named file and the design is otherwise unchanged.
- **A missed explicit `DE1Qt` call site would read a destroyed store** → a test asserts the string
  `"DE1Qt"` appears nowhere in `src/` outside the migration unit; the conversion is grep-driven and
  exhaustive rather than sampled.
- **Accessibility settings could be clobbered by a re-run of the older legacy migration** →
  addressed by the ordering constraint in D3; covered by a spec scenario and a test that constructs
  `AccessibilityManager` against a freshly-migrated store.
- **`cfprefsd` resurrecting a deleted plist on macOS** → clear-then-remove ordering, with removal
  failure treated as non-fatal.
- **Android/iOS file removal semantics differ from desktop** → destruction is best-effort by
  design; the keys are cleared through the API on every platform, which is what correctness
  depends on. Verify on a real Android device before merge, per the platform-code rule.
- **A user who upgrades then downgrades loses settings** → accepted, out of scope (Non-Goals).

## Migration Plan

1. Verify the store-identity assumption (D1) with a test assertion.
2. Land `AppSettings` **already pointing at the canonical identity**, and convert every call site.
3. Add the migration, gated to run before any settings object is constructed.
4. Extend `factoryReset()`, update the reference docs, and delete the now-obsolete warning
   comments about bare `QSettings()`.

**Revised during implementation.** This plan originally staged step 2 with `AppSettings` still
`DE1Qt`-identified, described as "a pure refactor, no behaviour change" and offered as a safe
stopping point. That was wrong. It holds only for the ~14 explicit-`DE1Qt` sites; for the ~34 bare
sites the same conversion *moves* the key's home from the canonical store into `DE1Qt`, and the
step-3 migration runs `DE1Qt → Decenza`, so it would never carry them back — web-auth sessions, UI
language, shot-map location, AI conversations and `internal/lastIconRegisteredVersion` would all
silently read empty. Pointing `AppSettings` at the canonical store from the outset reverses which
half is trivial: the bare sites become genuine no-ops, and every key that does move is moved by the
migration, which is the one piece built to verify itself.

The consequence is that steps 2–4 are **not separable**: between them the legacy `DE1Qt` keys are
unreachable. They ship as one change.

**Rollback**: a plain code revert works until the migration has run on a user's machine. After
that, reverting restores code that reads a `DE1Qt` store which no longer exists, so a real rollback
means shipping a revert *plus* a mirror migration.

## Open Questions

- Should the migration also run on a fresh install where no legacy store exists? Proposed: yes,
  unconditionally — it is a no-op that stamps the flag, and the `legacyKeyCount == 0` breadcrumb
  distinguishes "nothing to migrate" from "already migrated" in support logs.
- Is `SettingsSerializer`'s export/import format affected? It reads raw keys from the store via
  `Settings::testQSettingsPath()`-aware handles, so nominally no — confirm during implementation
  that no exported payload embeds a store name.
