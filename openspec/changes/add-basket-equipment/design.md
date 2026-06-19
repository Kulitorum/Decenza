# Design — Basket as an Equipment Component

## Context

`add-equipment-packages` modeled a package as a container of typed `EquipmentItem`
rows keyed by `kind`, with the grinder as the only kind today and the explicit
intent that baskets/tampers slot in as new kinds with no schema migration. PR #1344
landed `BasketAliases` — a curated, AI-first basket DB — with no consumers. This
change connects the two. The driving goal is reproducibility ("what gear made this
coffee") and AI dialing help, not dose management.

## Decision 1 — Basket lives *inside* the package (two baskets = two packages)

**Chosen:** the basket is a second `equipment_items` row owned by the package, and
it participates in the package's identity. Rejected alternatives: (B) basket as an
independent switchable axis with its own `shots.basket_id`; (C) basket on the
coffee bag.

**Why:**
- **Reproducibility is free.** `shots.equipment_id` already snapshots the package,
  so capturing the package's basket needs **no new shot column and no migration**.
  B and C both add a snapshot column + migration to buy only a cleaner inventory
  list — a poor trade against the north star.
- **Dial memory becomes `(grinder + basket)`-scoped**, which is more correct: a
  restrictive basket wants a finer grind, so remembering the dial per gear-combo is
  the right behavior, and it falls out of the package owning both.
- It is what the storage was explicitly designed for and what the user asked for.

**Accepted cost:** N grinders × M baskets is combinatorial. Mitigated because most
users run 1–3 baskets, the existing MRU + identity-dedup machinery surfaces the
recent combo, the basket is **optional** (grinder-only packages remain valid), and
inventory cards can group by grinder later if the list gets noisy. Each distinct
combo genuinely *is* a distinct reproducible config.

## Decision 2 — Identity widens to `(grinder + basket)`; CoW inherits it

The copy-on-write engine dedups/forks on package identity. Today that key is the
grinder's `brand/model/burrs`. It widens to include the basket's `brand/model`
(with "no basket" as a real, distinct value):

```
  switch basket on a USED package ──► fork a new package (copy-on-write)
  switch back to a prior combo    ──► dedup matches → select the old package
  edit basket on an UNUSED package ─► edit in place
```

No new control flow — only the comparison key in `findPackageBy…IdentityStatic`
and `supersedeOrEdit…Static` grows. This is the whole of the storage-engine change.

## Decision 3 — Basket specs are derived at read time, not cached

A grinder item stores `burrs` (user free-text) + `rpmCapable` (derived). A basket
has **no** free-text sub-field — its identity *is* `brand + model` — and every spec
(wall profile, relative flow, precision, dose range, material) is a pure function of
that identity via `BasketAliases::findEntry()`. So the basket item persists only
`kind/brand/model` with an empty `attrs`, and resolution derives the specs on load
(mirroring `rpmCapable` derivation). A custom/off-registry basket is a bare name
with unknown specs — acceptable; the advisor simply gets less to reason with.

Rejected: caching specs in `attrs` at write time. It duplicates the registry,
risks drift when the curated DB is refined (as #1344 just did), and buys nothing —
the lookup is a cheap in-memory vector scan.

## Decision 4 — The model picker needs a differentiator subtitle (new primitive)

The picker is **vendor-first, two-level** (brand → model): size lives in the model
name ("Ridgeless 18g"), so there is no third level analogous to the grinder's burrs.
But a bare-string model list is **insufficient**, proven by two real cases in the
shipped DB:

- **S-Works (6 models)** — Convex/Tapered/Step-Down/Stamped/Titanium billets are
  jargon; the name alone doesn't tell a user what differs. Differentiator: wall
  profile + material.
- **Decent (10 models, the trap)** — *Slightly* Waisted **14g** and *Very* Waisted
  **14g** have the **same dose** (overlapping 12–15g / 12–16g ranges); only the
  taper *degree* separates them. A subtitle that echoed dose would leave them
  indistinguishable. Differentiator: effective bed Ø + body.

This yields two regimes within one brand:

| Within-brand differentiator | Subtitle should lead with |
|---|---|
| dose-indexed (Decent Ridgeless 15/18/20/22) | dose (already in the name; subtitle is confirmatory) |
| taper-degree (Decent Waisted trio) | effective bed Ø + body — **never** dose |
| wall/material (S-Works billets) | wall profile + material |

**Decision:** the model row renders `name` + a **differentiator subtitle** sourced
from `summary()`. Baseline: render `summary()` uniformly (safe everywhere, never
hides the differentiator). Optimization (polish, not required): order the subtitle
to lead with whatever varies within the brand. `SuggestionField` is extended to
optionally accept `[{value, description}]`; the brand level stays a plain typeahead
(~15 vendor names need no subtitle).

**Optional refinement:** group a brand's models by sub-family (Decent →
`Ridgeless / Ridged / Waisted`; S-Works → billet shapes / stamped). Decent at 10 is
the count ceiling and the brand that earns grouping; it also gives the Waisted
adjectives a shared scale. Not required for v1.

## Decision 5 — AI context: fold the basket into `currentBean`

The basket is pure identity + derived specs; unlike `grinderContext` it has no
"observed settings" to aggregate across history. So it folds into the existing
`currentBean` block (which already carries grinder identity) rather than a separate
`basketContext` block:

```jsonc
"currentBean": {
  // …existing bean + grinder fields…
  "basket": {
    "brand": "Weber Workshops",
    "model": "Unibasket 18g",
    "wallProfile": "straight",      // human-readable string, not a code
    "relativeFlow": "open",         // THE cross-basket signal
    "precision": true,
    "doseRangeG": { "min": 17, "max": 19 }
  }
}
```

- **`relativeFlow` leads the reasoning value.** It is coarse and directional by
  design (Restrictive/Standard/Open) — it gives the advisor the *direction* of a
  grind change across baskets, never a magnitude. Reliable magnitude still comes
  from the user's own per-combo shot history (the `equipment_id` linkage), not a
  spec number.
- **`doseRangeG` enables a sanity signal** — the advisor can flag "22 g in a basket
  rated 17–19 g." Cheap, high-value, uniquely enabled by this data.
- Omit the whole sub-object when the package has no basket or the basket is custom
  (unknown specs) rather than fabricating fields.

## Relationship to `add-basket-settings`

`add-basket-settings` is a stale pre-equipment-packages proposal framing the basket
as the home for *dose*. This change deliberately does **not** take that on: dose
ownership stays with the bean/recipe per the current architecture, and the basket's
dose *range* here is a read-only advisory only. `add-basket-settings` should be
archived/withdrawn or re-scoped to "default dose" separately; it is not a dependency.

## Risks / Open Questions

- **S-Works flow-pattern SKUs.** The "Billet Basket" entry collapses
  Reduced/Standard/High flow into its notes, exposing only `Standard`. If users own
  different flow patterns, those arguably deserve separate registry entries — which
  would make the differentiator subtitle *mandatory* to tell them apart. DB-owner
  call (the data was just refined in #1344); the subtitle design makes that
  granularity survivable but this change does not edit the data.
- **Auto-name of a grinder+basket package.** The package name default is
  `"{brand} {model}"` (grinder). Whether the card title should incorporate the
  basket ("DF64 · VST 18g") or keep grinder-as-title with the basket on a sub-line
  is a display choice settled in `equipment-inventory-view`; the lean is
  grinder-title + basket sub-line (mirrors burrs).
