## Context

`remove-default-shot-rating` (#1561) deleted the "Default Shot Rating" setting
because it was auto-stamping every saved shot, which suppressed the AI taste
intake. It removed `SettingsVisualizer::defaultShotRating` and the cross-domain
wiring that mirrored it onto `SettingsDye::dyeEspressoEnjoyment` — but left
`dyeEspressoEnjoyment` itself in place.

`MainController` reads that field into `ShotMetadata::espressoEnjoyment` at three
shot-save sites and resets it to 0 *afterwards*. The ordering is what made the
residue survive: the field is persisted, so the last value written before the
upgrade was still in the store on first launch of the new build, got stamped onto
the next shot saved, and only then reset. Exactly one shot per upgrading user,
self-healing thereafter — a defect shaped to escape both testing and a bug report.

Observed in the field on 2026-07-19: a shot saved `enjoyment0to100: 50` with the
store holding `shot.defaultRating = 50` and `dye.espressoEnjoyment = 0`. That shot
never showed its taste intake, because the gate in `ConversationOverlay.qml` treats
any non-zero enjoyment as feedback the user already gave.

## Goals / Non-Goals

**Goals:**

- A shot's rating originates only from a person; no settings path can supply one.
- Leave no stored key from the removed feature — evicted, not merely unread.
- Make the invariant explicit in the specs, since its absence is why a
  settings-sourced rating looked unremarkable to reviewers.

**Non-Goals:**

- Correcting shots already saved with a rating the user's configured default
  produced. See "Decisions".
- Restructuring the migration chain, collapsing migrations, or introducing a
  schema floor. Migrations 7, 8, 20 and 21 carry data semantics, so a squash
  either replays them or silently corrupts an old database; that is a deliberate
  change on its own terms, not a side effect of deleting a settings read.
- Deprecating `settings_set dyeEspressoEnjoyment` gradually. It is removed
  outright: it was a footgun by construction, writing a rating that landed on
  whichever shot finished next.

## Decisions

**Delete the field rather than zero it at each read site.** Three call sites read
it and two reset it; patching all five leaves the mechanism intact and the sixth
caller a future accident. With the property gone, sourcing a shot rating from
settings is a compile error.

**Migration 16 resets inferred rows to 0, not to the configured default.** Its
original design chose the user's default over 0 on two grounds, both since
falsified: (a) "most users have a non-zero default" — the feature no longer
exists, so reading the key writes a number from deleted code; (b) "0 would display
as Rated 0/100 on Visualizer" — the PATCH builder sends `null` for 0, so the
back-sync clears those shots to Unrated. Removing the read is what makes evicting
`shot/defaultRating` safe, since migration 16 was its last reader.

**Old shots keep their ratings; inferred rows do not.** The distinction is who
chose the value. `enjoyment_source = 'inferred'` means the app computed a score
nobody picked, so resetting it destroys nothing — that was already the finding of
`remove-inferred-shot-ratings`. A rating produced by the user's configured default
is different: they chose that default, it reflects a real preference, and many of
those shots are already on Visualizer, where a local rewrite would desync the two
copies to "fix" something that is not wrong. History stands.

**Evict on construction, unconditionally.** `QSettings::remove` on an absent key
is a no-op, so two unguarded calls in the `Settings` constructor are idempotent and
free after the first launch. A version counter or migration framework would be
machinery for a two-line one-time cleanup.

## Risks / Trade-offs

**MCP surface break.** `settings_get` no longer returns `dyeEspressoEnjoyment` and
`settings_set` rejects it. Any script writing it must move to `shots_update
enjoyment0to100`. Accepted: the setting could not target a specific shot, so no
correct script could have depended on it.

**A pre-v16 database now resets inferred rows to 0 rather than to the user's
old default.** Those shots become unrated instead of carrying a number the user
once configured. Accepted, and arguably the repair: they were auto-stamped without
consent, unrated is the honest state, and it makes the taste intake reappear for
them.

**Eviction is not verifiable by the test suite alone.** `tst_settings` proves it
against the test store; whether a real plist is cleaned depends on the constructor
running on a store written by the old build. Verified by re-reading the live store
after a relaunch rather than inferred from a green suite.
