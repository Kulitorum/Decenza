# Measured results

All numbers below come from `tools/saw_parity`, which drives the real
`SettingsCalibration::getExpectedDripFor` / `addSawLearningPoint` over a corpus in
chronological order. No simulator is involved. Reproduce with:

```
saw_parity --corpus tools/saw_replay/data/baseline_extended.json
```

## The corpus

`tools/saw_replay/data/baseline_extended.json` — 250 shots, 2026-04-01 to 2026-08-23.

The previous corpus (`baseline_full.json`, 63 shots) is carried forward unchanged as part 1;
part 2 adds 187 shots harvested 2026-08-23 from a full `/api/database` download off the
device. The two parts are disjoint in time: no shot older than 2026-04-30 still retains a SAW
`Learning` line in the device database, so nothing overlaps and nothing was deduplicated.

The device database has been renumbered since April — its id 815 is 2026-04-19 where the old
corpus's id 815 is 2026-04-01 — so part 1's ids are stale. They are carried as `legacy_id` and
offset by +10000 to stay collision-free; the archived analyses' "shot 887" is `10887` here.

| | rows |
|---|---|
| flow low (<1.5) / mid / high (>=3) | 75 / 161 / 14 |
| basket `decent-18g-ridged` / `graph-coffee-stepped-58-46mm` | 243 / 7 |
| scale `Decent Scale` / `decent-wifi` / `decent` | 104 / 96 / 50 |
| profile `d_flow_q` / `80_s_espresso` / `d_flow_q_copy` | 223 / 25 / 2 |

`scale` is recorded verbatim as the pool key each shot actually trained, not normalized: the
three-way fragmentation is what the device experienced.

## Baseline: the shipped model

| bucket | n | MAE (g) | worst (g) |
|---|---|---|---|
| overall | 250 | 0.4008 | 5.98 |
| low (<1.5) | 75 | 0.3539 | 2.54 |
| mid [1.5,3) | 161 | 0.3613 | 4.07 |
| high (>=3) | 14 | 1.1070 | 5.98 |

Mean signed error is −0.102 g overall (median −0.033 g): the predictor under-predicts drip on
average, but the mean is carried by a handful of very large misses rather than a general bias.
The four worst errors are shots whose recorded drip is 3.4–8.1 g on a 36 g target — measurement
artifacts, not drip. Excluding rows with drip > 3 g (9 of 250) moves overall MAE to 0.3233 g.

**The high-flow bucket is 3x worse than the others.** That, not the commit step, is where the
model's error lives.

## The median-lag commit fix

Committing the median-lag shot's own `(drip, flow)` instead of `medianOf(drips)` over
`medianOf(flows)`:

| bucket | n | shipped | fixed | delta |
|---|---|---|---|---|
| overall | 250 | 0.4008 | 0.3967 | −1.0% |
| low (<1.5) | 75 | 0.3539 | 0.3529 | −0.3% |
| mid [1.5,3) | 161 | 0.3613 | 0.3531 | −2.3% |
| high (>=3) | 14 | 1.1070 | 1.1334 | **+2.4%** |

Worst-case error is unchanged (5.98 g, same shot). 218 of 250 predictions change; 111 improve
and 107 get worse, mean change −0.005 g. Excluding the drip > 3 g artifacts: 0.3233 → 0.3167.

**Verdict: this is a correctness fix, not a performance one.** The ~1% overall improvement is
inside the noise of which shots land in which batch, and the high-flow bucket regresses. It
should ship on the argument that a committed entry must describe a shot that happened — every
reader divides or smooths that pair — with the numbers recorded as showing the change is
approximately free, not as showing it is an improvement.

The earlier indicative figures (−8.5% overall, +5.9% worse at low flow) came from an ad-hoc
Python simulator and reproduced neither in magnitude nor in sign. They are withdrawn.

## Correction: `medianLag` is not currently computed

The proposal said the batch's median lag is "already computed one block above for the outlier
gate, then discarded". That is wrong. `addSawPerPairEntry` computes `medianLag` as
`medianDrip / medianFlow` — the same quotient of independent medians that the commit stores.
The `lags` vector is built, but its median is never taken. The outlier gate therefore also
compares each shot's lag against a lag no shot had. The fix takes `medianOf(lags)` and then
finds the batch shot that produced it.

## Does the basket segment earn its place?

`saw_parity --ignore-basket` replays with every shot filed under one basket, as a two-segment
`(profile, scale)` key would.

| slice | n | basket in key | basket ignored | delta |
|---|---|---|---|---|
| whole corpus | 250 | 0.4008 | 0.3994 | +0.0014 |
| since the first Graph shot | 22 | 0.2804 | 0.2645 | +0.0160 |
| Graph-basket shots only | 7 | 0.3414 | 0.3244 | +0.0171 |
| Decent shots in that window | 15 | 0.2520 | 0.2365 | +0.0155 |

Keying by basket is **worse in every slice**, including on the Graph shots the split exists to
protect. Only 11 of 250 predictions differ at all.

### The finding that justified the split has not reproduced

`2026-08-11-key-saw-learning-by-basket` measured, on 3 Graph shots, drip of 0.52–0.70 g against
the Decent basket's 1.00–1.48 g — roughly half — and stated its own evidence limit plainly
(n=3, one device, one profile). Four more Graph shots have since been recorded:

| date | duration | grind | flow (g/s) | lag (s) |
|---|---|---|---|---|
| 2026-08-09 | 52.1 s | 15 | 1.48 | 0.446 |
| 2026-08-10 | 52.3 s | 16 | 1.30 | 0.538 |
| 2026-08-11 | 58.3 s | 16.5 | 0.80 | 0.650 |
| 2026-08-21 | 34.8 s | 17 | 1.50 | 1.033 |
| 2026-08-22 | 32.1 s | 17.5 | 1.85 | 0.795 |
| 2026-08-22 | 35.6 s | 17 | 1.72 | 0.913 |
| 2026-08-23 | 33.9 s | 17.25 | 1.91 | 0.785 |

The first three are the original n=3. The four since 2026-08-21 have a mean lag of 0.882 s
against the Decent basket's 0.819 s over the same window — Graph slightly **higher**, the
opposite sign to the finding that motivated the split. Over the whole two-basket window the
between-basket mean lag gap is 0.082 s against a pooled within-window standard deviation of
0.145 s (d = 0.57), and a within-basket lag standard deviation of 0.410 s (CV 56%) across the
corpus. The gap is about a fifth of the noise it would have to be detected against.

What separates the two Graph groups is not the basket: the original three ran 52–58 s at grind
15–16.5, the later four run 32–36 s at grind 17–17.5. Duration correlates with lag at r = −0.669
inside that window — but at r = −0.007 across all 187 part-2 shots, and shots over 45 s have a
*higher* mean lag (1.078 s vs 0.744 s) corpus-wide. So duration does not generalize as an
explanation either. Both stories rest on the same three shots.

**The defensible statement is that no basket effect is demonstrated, not that none exists.**
Seven shots on the second basket is thin, and the archived change's own reasoning — isolation
only asserts the populations *may* differ, and a gap of zero costs only graduation speed — is
still sound as far as it goes. What the measurement adds is that the cost is not zero: the
split is worth −0.016 g of MAE in the window where it applies, because a fresh basket bucket
restarts from bootstrap.
