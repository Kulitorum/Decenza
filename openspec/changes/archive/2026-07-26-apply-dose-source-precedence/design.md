# Design

## Context

The dose reaches `Settings.dye.dyeBeanWeight` — the single live value every shot reads — from four
places today:

| Writer | Site | How it currently "wins" |
|---|---|---|
| Profile load | `ProfileManager::loadProfile` ([profilemanager.cpp:1617](src/controllers/profilemanager.cpp:1617)) | queued write, unconditional |
| Recipe activation | `MainController` ([maincontroller.cpp:1543](src/controllers/maincontroller.cpp:1543)) | queued write, queued **after** loadProfile's so it lands last |
| Bag selection | `SettingsDye::applyBag` ([settings_dye.cpp:717](src/core/settings_dye.cpp:717)) | direct write, unconditional |
| Shot / auto-favorite replay | `MainController` ([maincontroller.cpp:835](src/controllers/maincontroller.cpp:835)) | queued write, same trick as recipe activation |

Precedence is therefore **implemented by event-queue ordering**, and only in the direction
"activation happens after the profile load it triggered". The comments say so outright: *"queued to
run after ProfileManager::loadProfile's own deferred setDyeBeanWeight(recommendedDose) … so ours
wins."* Nothing enforces the ladder in the other direction — a recipe already active when the user
loads a **different** profile gets no second write, so the profile's dose lands and stays.

That is often worse than a wrong session value, because `dyeBeanWeightChanged` is wired to a
write-through stamp:

```cpp
connect(m_settings->dye(), &SettingsDye::dyeBeanWeightChanged, this, [this]() {
    stampActiveRecipe(QStringLiteral("doseG"), m_settings->dye()->dyeBeanWeight());
});
```

and `SettingsDye::setDyeBeanWeight` calls `writeThroughToBag("doseWeightG", value)`. Where the
overwriting write lands while a source is still active, it does not merely show the wrong number
for one shot — it rewrites that source's stored dose.

Which sources actually get corrupted is worth being exact about, because it is not uniform (this
was overstated in an earlier draft and corrected by a live run):

| Path | Live dose | Stored data |
|---|---|---|
| Profile load, **bag** active | wrong | bag's `doseWeightG` **rewritten** — no deactivation exists for bags |
| Profile load, **different** profile, recipe active | wrong | recipe row **intact** — `MainController`'s title-mismatch watcher deactivates the recipe during `currentProfileChanged`, which fires after the write is armed but before it runs |
| Profile load, recipe's **own** profile reloaded | wrong | recipe's `doseG` **rewritten** — no title mismatch, so no deactivation |
| Bag apply (`bagReady` from an external bag edit), recipe active | wrong | recipe's `doseG` **rewritten** — the wide exposure; `m_applyingBag` suppresses the write-through to the bag, not the stamp |

`yield-anchor` already solved this shape for the yield, and `coffee-bag-model` records the rule it
settled on — enforce the ladder explicitly, *"never left to emerge from the order in which the
bag-selection and recipe-activation signals happen to arrive."* This change applies the same rule
to the dose.

## Goals / Non-Goals

**Goals**
- One ladder — recipe → bag → profile — enforced by an explicit check at each write site.
- Loading a profile can never overwrite, or cause a stamp over, a higher-priority dose.
- A dose edited with no recipe and no bag active has somewhere to persist.
- One MCP spelling for the per-profile dose.

**Non-Goals**
- Changing how a dose is *stored* on a profile, bag or recipe. Storage is unchanged.
- Touching grind/RPM write-through. Dose only.
- Re-litigating the yield ladder, or unifying dose and yield behind one resolver — the yield's
  intent/measurement split means they legitimately differ (button-protected vs. auto-remembered).
- Migrating already-stored doses. This is now-and-future behaviour.

## Decisions

### Where the ladder is resolved

**`SettingsDye` owns the answer.** It already holds `activeRecipeId` and `activeBagId`, it already
applies the bag, and every writer that needs the gate (`ProfileManager`, `MainController`, the MCP
tools) holds a `Settings*`. A new read-only accessor:

```cpp
// SettingsDye
enum class DoseOwner { Recipe, Bag, Profile };
Q_INVOKABLE DoseOwner doseOwner() const;   // registered for QML
```

resolving as: recipe if `activeRecipeId > 0 && m_activeRecipeDoseG > 0`, else bag if
`activeBagId > 0 && m_activeBagDoseG > 0`, else profile.

Two small caches back it. `m_activeBagDoseG` is set in `applyBag` (which already reads
`bag.value("doseWeightG")` locally) and cleared when no bag is active. `m_activeRecipeDoseG` is
pushed down by `MainController`, which owns the recipe map.

**The recipe's id and dose are set in one call**, `setActiveRecipe(recipeId, doseG)`, rather than a
separate `setActiveRecipeDose`. Two setters make "the cache belongs to the recipe the id names" a
convention that every call site has to remember, and the id then advances on paths the dose does
not: activation sets the id last (deliberately — watchers must not see a half-applied recipe), and
the startup restore and external-edit re-read both repopulate the recipe through `recipeReady`,
which is not an activation at all. Folding them together makes the pairing structural, and gives
those paths an obvious call site instead of a missing one.

**Each rung also tracks whether its row has arrived** (`doseLadderResolved()`). An id is set
synchronously; the row that says what dose it designs comes back from a storage worker. In between,
an unresolved rung is indistinguishable from a source that designs no dose — and the caller that
acts on that reading, the profile's recommended dose, is the destructive one: it writes through to
the bag's stored dose and stamps the recipe's. So the ladder refuses to answer rather than answering
wrongly, and the writer declines while it is unresolved.

That refusal is also why the profile's write **resolves the ladder when it lands, not when it is
armed**. Both are deferred a turn; an arm-time check reads the selection window as "nobody else
supplies a dose", and the write then arrives after the bag's row has applied.

*Rejected:* putting the resolver on `MainController` (owns `m_activeRecipe`, so no recipe cache
needed) — `ProfileManager::loadProfile` is the site that most needs the gate and has no
`MainController`. *Rejected:* deriving "supplies a dose" from the live `dyeBeanWeight` on the
grounds that write-through keeps bag and recipe in sync with it — true while they hold a dose,
false for a bag or recipe that holds none, which is exactly the case the ladder must skip.

### Gating the four writers

- **Profile load** — wrap the existing queued write in `if (doseOwner() == DoseOwner::Profile)`,
  and additionally skip it entirely on the startup load. This is the behaviour change users notice.
- **Recipe activation** — keep the write; it is the ladder's top rung, and **keep the queued
  dispatch**. The original plan was to drop it as redundant once the gate exists. That is wrong:
  `applyActivatedRecipe` calls `loadProfile` at its top, and at that moment `activeRecipeId` is
  still the *previous* recipe's (or −1 on a first activation), so the ladder names the profile and
  its deferred write is already armed. A synchronous recipe write would land first and be
  clobbered. The two mechanisms cover different collisions and both are needed: the queue for the
  profile load this activation itself triggered, the ladder for every other one. Documented at the
  site rather than left to be rediscovered.
- **Bag selection** — gate `applyActiveBag`'s dose write on no recipe supplying one. This is the
  asymmetry `coffee-bag-model` already fixed for the yield spec and left standing for the dose, and
  it closes a live bug: an external edit to the active bag (bag dialog, Next Portion, post-shot
  stamp) re-enters `applyActiveBag` via `bagReady`, and the unguarded write put the bag's dose over
  an active recipe's — which the `dyeBeanWeightChanged` stamp then wrote back into the recipe row.
  `m_applyingBag` suppresses the write-through to the bag, not the stamp.
- **Shot / auto-favorite replay** — **outside the ladder, unchanged.** Loading a shot restores that
  shot's own recorded dial-in; it is not one of the three standing sources. It keeps its queued
  write and therefore still lands last, for the same reason recipe activation does.

### Where a dose edit lands — and why the profile is not a target

**No profile write target is added.** The plan was to add one to
`ProfileManager::activateBrewWithOverrides` (the Brew Settings OK path and the MCP brew control),
gated on `doseOwner() == Profile`. Reading the only available call killed it:
`setCurrentProfileRecommendedDose` sets `m_profileModified = true`, and `activateBrewWithOverrides`
runs on **every** OK regardless of whether the dose changed — so every Brew Settings commit would
mark the loaded profile modified. Scoping it to genuine changes fixes the false positives but not
the premise: dirtying a profile from a dial-in nudge is the wrong behaviour, and a profile's
recommended dose is stored design.

So the ladder governs which source is *read*, and its top two rungs keep the write targets they
already have (the recipe stamp, the bag write-through) — both of which persist. A dose dialed with
neither active stays in `Settings.dye`, which also persists. The profile's recommendation is edited
where the rest of the profile is: the two recipe editors' Dose controls and the MCP `dose`
parameter, all of which already call `setCurrentProfileRecommendedDose` directly and are unchanged.

The corollary matters for the caches below: a dose dialed onto a recipe or bag that had none makes
that source the owner, so both caches must follow the write-through and the stamp, not just the
activation and the bag apply.

### Why the caches are not persisted

`m_activeBagDoseG` and `m_activeRecipeDoseG` are runtime-only, while `activeBagId` and
`activeRecipeId` persist. At launch the ids are restored immediately but the rows load
asynchronously, so the ladder would answer "profile" for a session a recipe actually owns.
Persisting the two doses would close that window; the resolution flags close it better, because the
window is not confined to launch — it reopens on every bag or recipe selection, and a persisted
cache would be just as stale there.

The startup skip stays as well, on its own terms: the live dose is already persisted from whichever
source won last session, so there is nothing to apply. It is not sufficient on its own, though, and
the first draft of this change treated it as if it were. `m_startupLoadDone` flips at the end of the
`ProfileManager` constructor, while `loadAutoLoadProfileIfNeeded()` fires later from QML — so for a
user with an auto-load profile pinned, the launch-time load was never gated by it.

### MCP: one spelling

`mcptools_profiles.cpp` keeps the `dose` handler (set-and-enable) and drops `recommended_dose` /
`has_recommended_dose` from the accepted edit keys, which removes the conflict rule and
`conflictedFields` with them. Reporting still emits the field with its flag, once, as
`recommendedDoseG` + `hasRecommendedDose` — the advanced branch's profile-JSON spread drops the
snake_case pair so a reader is not shown four keys for two fields and tempted to send back the
spelling that no longer writes.

The retired names are stripped explicitly (nothing validates incoming keys against the schema, so
the advanced branch's map loop would otherwise still apply them) and reported as `retiredFields`
with a note naming `dose`. Three details the first cut got wrong:

- The caveat rides on `message` as well, the way `ignoredFields` already does. A client that reads
  `success` + `message` and skips the siblings was being told a clean "Profile updated" for a call
  that dropped an argument.
- A call whose *only* keys were retired returns `success: false` and stops before the upload.
  Falling through ran `uploadProfile()`, which marks the profile modified — so a fully rejected edit
  dirtied the loaded profile and then told the caller to `profiles_save` it.
- `dose` is validated rather than coerced. It is the one key read straight as a number instead of
  through `toVariant()`, and a failed read yields 0 — which *clears* the recommendation. A
  stringified number is not the exposure (`normalizeArguments` coerces `"18"` off the schema's
  declared type); a value no parse can rescue is, and it would have deleted the profile's dose and
  reported success. A clamp is now reported in `adjustedFields` instead of the caller's number being
  echoed back as if stored.

## Risks / Trade-offs

- **A user who relied on the old behaviour.** Someone switching profiles to change the dose while a
  recipe is active will find the dose no longer follows. That is the bug being fixed, and the recipe
  is the thing they chose most recently; the Brew Settings dose remains one tap away.
- **Two mechanisms rather than one.** The queue ordering survives alongside the ladder, so someone
  reading either in isolation could conclude the other is redundant. Both sites now carry a comment
  saying which collision each covers — and the recipe-activation write's queueing is now justified
  on its own terms (it must land after the id is set) rather than by out-racing another write.
- **The rung caches must stay truthful**, or the ladder sits on a stale answer. This is the risk
  that actually bit: the first cut maintained them by hand at each write site, and the guard sets
  drifted — the startup restore never armed the recipe rung at all, and two sites claimed a rung for
  a value their persist had declined to write. Three things narrow it now: the id and dose are set
  together, each cache write carries the same guards as the persist it rides on, and an unresolved
  rung is refused rather than read as empty. Covered by tests in `tst_coffeebags` (the real async
  bag path) and `tst_profilemanager` (a real bag row read back after a profile load).
- **`MainController` still has no test fixture**, so which value reaches `setActiveRecipe` from each
  of its three call sites is verified by reading, not by a test. The pairing itself is covered.

## Migration Plan

None. Stored doses on profiles, bags and recipes are untouched; only the choice of which one is read
changes, and it changes on the next profile load or bag selection.

## Open Questions

None.
