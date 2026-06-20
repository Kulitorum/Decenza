# Design — Puck Prep as an Equipment Component

## Context

`add-basket-equipment` (merged, #1345) made the equipment package a switchable
bundle of typed `EquipmentItem` kinds (grinder + optional basket) with copy-on-write
identity and a `currentBean` sub-object feeding the AI advisor. Puck prep is the
next kind. The driving goal is the same as basket's: reproducibility + AI dialing
help. The novelty is that puck prep is **technique**, not a branded object — so it
reuses the storage/identity/AI machinery but breaks the vendor-picker UI and the
brand/model identity shape.

## Decision 1 — Package-identity placement (not per-shot)

**Chosen:** puck prep is a component of the package, at the **identity** level
(like basket) — a distinct prep is a distinct package, switched/forked the same way.
Rejected: per-shot capture ("did you WDT *this* pull?").

**Why:** the user's framing — "most people use only one or two preps and switch
between them using packages." A package already means "the gear I used"; extending
it to "the gear + prep routine I used" is the natural unit. Per-shot would be more
diagnostically precise but adds friction on every shot and plumbing we don't need
for the 1–2-routine reality. Cost accepted: the combinatorial grid grows to
grinder × basket × prep, but each axis is small (1–3), and MRU + dedup keep the
inventory list sane — the same bargain basket struck.

## Decision 2 — Identity is a normalized flag-set (the one real delta)

Grinder identity is `brand/model/burrs`; basket is `brand/model`. Puck prep has no
string identity — it is the **set of ticked flags**. So the copy-on-write
dedup/fork comparison normalizes the config to a canonical form and compares that:

```
  config { wdt:true, shaker:true, puckScreen:false, paperFilter:false, rdt:false }
     │  normalize (fixed key order / sorted true-flags / bitmask)
     ▼
  canonical "shaker,wdt"   →  compared for dedup & "unchanged?" checks
  no puckprep item         →  canonical ""  (a distinct, matchable value)
```

This is a small helper (serialize the flags canonically), not new architecture. The
identity-widening otherwise inherits everything basket built: toggle a flag on a
*used* package → fork; switch back to a prior combo → dedup; edit an *unused*
package → in place.

## Decision 3 — Derive the `distribution` rollup at read time

Store only the booleans on the item (`attrs`). Compute the **`distribution`**
rollup (`none` | `light` | `thorough`) at read time, exactly as basket specs are
derived from the registry (here the "registry" is a pure function, no table):

```
  distribution = (wdt || shaker) ? "thorough"   // equal weight — NOT ranked
               : rdt             ? "light"       // anti-static only, not active distribution
               :                   "none"
```

WDT and shaker are both deliberate puck-*distribution* techniques and are weighted
**equally** — which is "better" is genuinely contested (a good shaker / needle
distributor matches or beats mediocre WDT), so the rollup must NOT rank one above
the other; it answers "did the user actively distribute, or is this dump-and-tamp?".
`rdt` (anti-static declumping) counts as `light` on its own; `puckScreen` /
`paperFilter` are separate flags the advisor reads individually (kept out of the
rollup to
keep it legible). Rationale for derive-at-read: the rollup logic lives in one place,
can be tuned (or extended when *sift* lands) without touching stored data, and the
advisor always sees a current interpretation.

## Decision 4 — Checkbox UI, not a vendor picker

The Edit Equipment form's grinder/basket sections are `SuggestionField` vendor
pickers. Puck prep is a **row of labeled checkboxes** — a new small primitive
(a labeled `CheckBox` with `Accessible.role: CheckBox` + checked-state name). It
drops into the two-column form layout added in the basket PR. No `descriptions`
subtitle, no registry lookups, no flip-up dropdown — it's the simplest section in
the form.

## Decision 5 — All-unchecked = no puck-prep item (optional, like basket)

A package with every box unchecked persists **no** `kind="puckprep"` item (mirrors
basket-clear when brand+model are empty). Setting any flag upserts the item;
clearing the last flag deletes it. The `currentBean.puckPrep` sub-object is omitted
when there is no item.

**Known nuance (deferred):** this collapses "I deliberately do *no* prep" and "I
haven't recorded my prep" into the same absent state. The explicit-none case is
arguably the *strongest* "add WDT" signal for the advisor, but distinguishing it
needs an explicit "puck prep: none" affordance that's awkward with bare checkboxes.
v1 accepts the collapse; a later refinement could add an explicit toggle.

## Decision 6 — Content scope (lean checklist) + deferred follow-ons

Five flags, chosen by AI-dialing value (does knowing it change the advice?):

| Flag | Advisor use | Tier |
|---|---|---|
| `wdt` | channeled + no WDT → "WDT is the #1 fix"; + WDT → don't re-suggest it | high |
| `shaker` | same distribution axis as WDT (clump-breaking) | high |
| `paperFilter` | muddy cup / bottom channeling → "try a bottom paper" | moderate |
| `rdt` | clumpy/static grind, inconsistent dose → "spritz" | moderate |
| `puckScreen` | mostly cleanliness; keeps the advisor from misreading flow startup | low-moderate |

**Deferred follow-ons (additive, no migration):**
- **Tamper** — base shape (flat / convex / ripple) is a real *functional* axis
  (edge-vs-center extraction bias, fit-to-basket → edge channeling), distinct from
  the AI-noise of tamper *brand*. Adds as one enum field on the puckprep item.
- **Sift** — a 6th boolean, distinct from shaker. Adds as one flag; the
  `distribution` rollup folds it in.

**Excluded as AI-noise:** tamp pressure (settled physics past full compaction),
nutation, dosing funnel, and any *brand* strings — including these just trains users
to record data the advisor can't use.

## Decision 7 — AI consumption: the channeling branch

The payoff. The channeling detector reports *that* a shot channeled; `puckPrep`
tells the advisor what to do:

```jsonc
"puckPrep": { "wdt": true, "shaker": true, "puckScreen": true,
              "paperFilter": false, "rdt": false, "distribution": "thorough" }
```

```
  channeling detected
       ├─ distribution none/light  → "Add WDT or a shaker — biggest lever here"
       └─ distribution thorough    → "Prep's solid; it's grind/dose/basket — go finer"
                                       ↑ the valuable branch: don't tell a meticulous
                                         WDT-er to "try WDT"
```

`paperFilter` / `puckScreen` are read individually for their specific cases. The
sub-object is omitted (not fabricated) when the package has no puck prep.

## Shot resolution (mind the positional columns)

The shot's puck prep resolves via the `equipment_id` JOIN, like basket — a new
`LEFT JOIN equipment_items ep ... kind='puckprep'` in `loadShotRecordStatic`, its
`attrs` read and the flags parsed. **Append** the new selected column(s) after the
basket columns (44/45) and update the positional reads carefully — this is exactly
the silent column-shift the basket PR added a `tst_dbmigration` regression for;
extend that test to cover the puckprep column(s) too.

## Risks / Open questions

- **Rollup tuning.** The mapping is a judgment call; it lives in one derive-at-read
  function so it's cheap to revise (and must be revisited when *sift* lands). WDT and
  shaker are weighted equally on purpose — ranking either above the other would bake a
  contested opinion into advice the AI then acts on.
- **Checklist legibility.** Five bare checkboxes risk reading as clutter; grouping
  or a one-line "Puck prep" header keeps it scannable (UI detail, not architecture).
- **Explicit-none vs unknown** (Decision 5) — the only semantic the v1 model gives up.
