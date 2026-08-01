# Heal every pre-enrichment fork, not just the burr ones

## Why

Two rules live forty lines apart in `src/history/equipmentstorage.cpp` and
disagree about what enrichment is.

The **live rule** (`:1547-1553`) treats an edit as enrichment — apply in place, do
not fork — when *every* component that differs was empty before:

```cpp
const bool enrichment = !gainingAGrinder
    && filledIn(cur.brand, brand) && filledIn(cur.model, model) && filledIn(cur.burrs, burrs)
    && filledIn(curBasket.brand, basketBrand) && filledIn(curBasket.model, basketModel)
    && (curPuck.model.isEmpty() || curPuck.model == puck);
```

Six fields. The comment above it is explicit that basket and puck prep are
included on purpose: *"there is no espresso pulled without a basket, so an absent
basket is a basket nobody wrote down, exactly like absent burrs."* The spec says
the same (`equipment-package-model`, "Naming a basket that was never recorded is
enrichment").

The **heal** that retro-applies that rule to databases which forked before it
shipped (`healEnrichmentForksStatic`, `:1391`) tests **one** component:

```sql
AND TRIM(IFNULL(json_extract(og.attrs,'$.burrs'),'')) = ''
AND TRIM(IFNULL(json_extract(ng.attrs,'$.burrs'),'')) != ''
```

and additionally requires basket brand, basket model and the puck-prep string to
be **equal** — which is the exact inverse of enrichment for those components.

So a user whose fork was caused by recording a basket is not healed, and the log
says `merged 0 package(s)`, which reads as "nothing to fix".

Verified on the maintainer's Android export:

| pkg | created | grinder | burrs | basket | puck prep | shots |
|---|---|---|---|---|---|---|
| 1 | 2026-06-21 08:03 | Niche/Zero | 63mm Mazzer Kony conical | **—** | **—** | 961 |
| 2 | 2026-06-21 10:14 | Niche/Zero | *byte-identical* | Decent/18g Ridged | puckScreen,shaker | 59 |

Grinder identical, `superseded_by = 2`, forked two hours after the first package
was created — five weeks before the enrichment rule landed (`50182dbe`, #1720,
2026-07-30). Textbook pre-enrichment fork. Migration 35 skipped it because the
burrs already matched.

## What Changes

- **MODIFY** the heal's signature from "the burrs went empty→named, everything
  else equal" to "**every component that differs went empty→named**" — the same
  shape as the `enrichment` predicate above it, which is the rule the heal exists
  to apply retroactively.
- **ADD** a new migration to run the widened heal. Migration 35 already stamped
  success on databases that reached it, so its version cannot be reused. Most
  installed devices are on stable v2.0.0 at schema ≤ 34 and have never run it;
  they get the widened version on first launch instead.

Nothing else changes. Same merge machinery, same transaction handling, same
active-equipment relocation, same logging.

## Impact

- Affected specs: `equipment-package-model`
- Affected code: `src/history/equipmentstorage.cpp` (`healEnrichmentForksStatic`),
  `src/history/shothistorystorage.cpp` (a new migration beside migration 35)
- On the maintainer's database: packages 1 and 2 fold into one carrying all 1020
  shots. The grind step is unaffected — it already pools both packages by model
  string and already reports 0.25 from 28 settings.

### Deliberately NOT in scope

A `grinders` table. That was proposed and killed under review: two independent
passes measured today's model-string scope against a `grinder_id` scope on both
real databases and got identical results — 1020 shots, 28 settings, 0.25 — and the
change would have split burr-enrichment forks that currently share a history,
collapsed six distinct remembered dial positions into one, and deleted the heal
this change fixes. The fork here is not evidence the schema cannot model the gear;
it is evidence the heal is one component too narrow.

Also out of scope, and worth its own one-line change: `grinderModelMatchSql`
matches on model and ignores brand, so two makers sharing a model string would
pool. Exactly one genuine collision exists in the 196-entry registry (`m4` →
Macap, Versalab).
