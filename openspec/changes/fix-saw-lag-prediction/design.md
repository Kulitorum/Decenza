## Context

See `proposal.md` — Why, and `analysis.md` for the measured tables.

**The stored entry is read three ways.** `getExpectedDripFor` feeds `(drip, flow)` to the
Gaussian smoother; `sawLearnedLagFor` reads `drip / flow`; `globalSawBootstrapLag` takes the
median of pair lags. A fourth reader is inside the commit itself: the outlier gate compares
each shot's lag against `medianLag`. Anything that changes how the pair is derived changes
all four.

**The evaluation instrument works.** An earlier draft of this document asserted that
`tools/saw_replay` / `tools/saw_parity` had drifted out of usability and had to be repaired
first. That was wrong on every count and is recorded here so it is not re-derived:
`saw_parity` compiles unchanged against today's headers (`basketKey` has a default argument on
both entry points), both tools already have CMake targets, and `saw_parity` drives the real
production functions. What was actually wrong is smaller and worse: that same default argument
meant the tool passed no basket, so every corpus shot landed in one pool and the tool was
measuring a key shape that does not ship. One line.

## Goals / Non-Goals

**Goals:**
- A committed entry describes a shot that happened.
- The outlier gate compares against a lag some shot actually had.
- A corpus and a harness that can gate a future model change on real numbers.

**Non-Goals:**
- Changing the prediction form. Both alternatives are already on the record as worse.
- Changing σ, the recency schedule, the read window, the batch size, or graduation.
- Changing the basket segment of the key — see `proposal.md`.
- A settings migration. The entry keeps its shape.

## Decisions

### Store the median-lag shot's own pair, not a new field

`medianLag` becomes `medianOf(lags)`, and the commit stores the `(drip, flow)` of the batch
shot whose lag is closest to it.

The obvious alternative — add a `lag` field to the entry — was rejected. It makes the entry
self-contradictory the moment a reader trusts `drip / flow` instead (two of the three readers
do), it needs a migration for entries that lack the field, and it leaves the smoother with a
synthetic pair. Choosing a real shot fixes every reader at once and needs no schema change:
old entries keep parsing, they are simply less accurate until they age out.

Closest-lag rather than exact equality, even though `kBatchSize = 3` guarantees the median
belongs to exactly one shot: with an even batch size `medianOf` averages the middle two and no
shot owns the result. The loop stays correct if that constant ever changes.

`medianOver` stays an independent median. It gates the auto-reset, is never divided, and no
reader pairs it with drip or flow.

### Ship it on the correctness argument, with the numbers saying what it costs

Measured over 250 shots through the production code: −1.0% MAE overall, −2.3% mid-flow, +2.4%
worse high-flow (n=14), worst case unchanged, 111 predictions improved against 107 worsened.

That is not an improvement, and the change is not argued as one. It is argued as: an entry
that four readers divide or smooth must describe a shot that happened. The numbers are here to
show the correction is approximately free, and to stop a later reader citing it as a
performance result.

The alternative — hold the fix until it can be paired with a model change that does pay — was
rejected. It leaves known-wrong data being committed in the meantime, and it entangles a
correctness fix with a tuning decision that has already been reversed once for exactly that
kind of entanglement.

### Extend the corpus, and keep it honest about what it is

63 shots was too few to separate a 1% effect from batch-assignment noise; 250 is enough to say
"inside the noise" with confidence. Two things about the extension are load-bearing and easy to
get wrong later:

- **The device database has been renumbered.** Its id 815 is 2026-04-19 where the old corpus's
  id 815 is 2026-04-01. Matching by id silently pairs unrelated shots. The two harvests are
  matched by timestamp, found to be disjoint, and concatenated; the old ids are carried as
  `legacy_id` and offset by +10000.
- **`scale` is recorded verbatim, not normalized.** The same physical scale appears as
  `Decent Scale`, `decent` and `decent-wifi` across the corpus's span. Normalizing them would
  fabricate a pool maturity the device never had.

### Record the flow-bucket picture where a future proposal will find it

The high-flow bucket is 1.11 g MAE against ~0.36 g elsewhere, worst case 5.98 g. That is where
the model's error actually lives, and it is untouched by this change. The archive's low-flow
over-prediction (shot 887, now `10887`) and this corpus's high-flow error are both consistent
with the weighted average being too flat in flow — a hypothesis, not a conclusion, and the
harness now exists to test it.

## Risks / Trade-offs

**High-flow regresses 2.4%.** → Accepted. n=14, and the bucket's absolute error is dominated by
something else entirely. Documented in `analysis.md` rather than hidden.

**Old entries stay wrong until they age out.** A pool carries up to 10 medians, so a bad entry
can influence predictions for ~30 shots after the fix. → No migration: rewriting historical
entries would require the raw batches that produced them, which are not retained. The trim
resolves it.

**The corpus contains measurement artifacts.** Nine of 250 rows record a "drip" of 3.4–8.1 g on
a 36 g target, which is not drip. → Kept, because the previous corpus's skip rule
(`drip ∈ [-0.5, 10]`) was kept unchanged so the two harvests are comparable. `analysis.md`
reports the tightened figure alongside (0.3233 → 0.3167) so the conclusion does not rest on
them.

## Migration Plan

No data migration. Forward-only: entries committed after the change describe real shots,
entries committed before it are left alone and age out under the existing trim.

Rollback is a revert — no stored state changes shape, so a reverted build reads the same
entries it wrote.
