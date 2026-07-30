## Context

`fix-equipment-enrichment-fork` was archived onto the main spec before its PR was reviewed. The review then changed two of the behaviours that change had just specified, and added a third the spec never covered. The code is correct as of `848e2df7` and the tests pin it; only the spec is stale.

## Goals / Non-Goals

**Goals:**
- The main spec describes the code that shipped.
- The two corrections carry their reasons, since both look arbitrary without them.

**Non-Goals:**
- No code changes. If this change requires one, something has been mis-transcribed.
- Not a re-litigation of the enrichment rule itself — only its grinder-less exception.

## Decisions

**The grinder is the only component whose absence blocks enrichment.** The first cut of this guard rejected *any* absent component, and the existing whole-tuple enrichment test failed on it — correctly. The line is physical, not tidy: espresso cannot be pulled without a basket, so an unrecorded basket is unrecorded data, exactly like unrecorded burrs. Tea is genuinely made without a grinder, and a grinder-less package's shots must not begin claiming one. Alternative considered: block enrichment on any absence, for uniformity — rejected, because it would fork on a user naming the basket they have always used, which is the same complaint #1713 raised.

**A superseded target is not revived by a merge.** The revive exists so the package that ends up holding the history is selectable. That reasoning does not reach a package a *third* package replaced: it is still retired for its own reason, and reviving it produces a live duplicate carrying its successor's derived name. Reachable from the heal on a real chain, so it is a behaviour, not a defensive nicety.

**The log subsystem is spec'd, not left as implementation detail.** What `[Equipment]` must answer — which branch an identity edit took — is the diagnosis path for the whole capability. Registered markers are a published API here (renaming one breaks every saved filter), so the requirement names the guarantee rather than the helper.

## Risks / Trade-offs

- **A spec change with no code change can drift from the code again.** → The scenarios map one-to-one onto existing tests (`copyOnWriteAndMerge`, `healEnrichmentForks`, `equipmentMergeMovesHistoryAndDeletesSource`), so the next divergence fails the suite rather than only reading wrong.
- **The grinder-less exception is a special case in a rule whose value is being simple.** → Accepted and written into the requirement with its reason, so it is not "corrected" back out by someone who sees only the asymmetry.

## Migration Plan

None — spec-only.

## Open Questions

None.
