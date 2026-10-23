# Design

Two tests, each grounded in what drip physically cannot do, measured against what the shot itself did at the stop. The thresholds come from one DE1 and one Half Decent Scale (the only post-stop data available: Visualizer stores neither the debug log nor the raw scale stream, for any app), so each sits well clear of the clean-shot maximum and the estimator is the one SAW already relies on for every scale.

## The flow test (every beverage)

After the pump stops, drip only slows. A hand or a pressed scale makes the weight rise faster. Two readings in a row whose short flow is more than 4 g/s above the reference are a disturbance. The reference is the short flow at the SAW trigger, or at the first settling sample if higher: shot 939's trigger caught the pour at 4.3 g/s, but it was running 7.5 g/s when the pump stopped.

The flow is `WeightProcessor`'s estimate, now `ScaleFlow::slopeGps` so the replay shares it: de-jittered (batched BLE delivery), and widened to cover three samples on slow scales (1 s at 2 Hz). It is what SAW stops on with every supported scale, so a scale too noisy for it would already misstop.

| | excess over reference, g/s |
|---|---|
| Clean shots (358) | p99 0.95, max 2.38 |
| Disturbed shots (30) | 23 above 4 |

A draining filter bed slows down, so it never trips this test. Decaid uses the same idea (a 3 g/s jump between samples locks the yield).

## The amount test (espresso group only)

`clamp(3 s × flowAtStop, 5 g, 12 g)` above the settle base, on two consecutive samples. Measured on the tablet's SAW shots with stored weight curves (Oct 2026):

| | value |
|---|---|
| Undisturbed shots: drip after settling started ÷ flow at stop | p99 1.74 s, max 2.17 s (362 shots) |
| Largest real drip | 6.85 g at 4.3 g/s, a gusher SAW caught late (shot 939) |
| Short flow at stop, spiked readings | 26 and 60 g/s (shots 758, 897) |
| Disturbed shots (replay) | 24 of 406, peaks 5.7–36 g over the stop |

It catches slow drifts the flow test cannot: shot 881 crept to 61 g at 3.6 g/s, under the flow margin. The flow term keeps real fast drips: a flat 5 g, like the scale-fault branch this replaces, would have discarded shot 939's 6.85 g. The ceiling keeps a spiked flow reading from switching the check off.

An earlier draft used 1 s × flow, sized from shots whose SAW stop landed on target. It discarded shot 939; late-trigger gushers are where the large real drips are.

Filter, pourover and tea (`BeverageType::group`) skip it: their beds keep draining standing water after the stop, which can pass any amount an espresso puck drips. No filter shot exists in the corpus, so this is reasoned, not measured; the flow test still covers them.

## The base

Drip is measured from `max(stop weight, weight when settling started)`. Settling starts when the machine stops flowing, so a stop the DE1 acts on late (a BLE retry) leaves the extra pour in the base instead of reading it as a disturbance. On normal shots the two differ by p99 0.33 g.

## Two samples, and the quiet scale

One disturbed reading is dropped: a corrupted packet is not a hand, and feeding it to the window would break the stability gate. But if settling would complete (silence override, timer, timeout) while one is pending, it counts: shot 1016 sent one reading 5 g over and then nothing for 2.3 s, and completing on the reading before it saved a value the disturbance had already moved.

The rejected reading still lives in the worker's flow window. A return to the last accepted weight's 0.2 g band cancels confirmation even if flow remains high, and flow-only candidates in that band are ignored until flow normalizes or weight rises outside it. This event-based recovery is shared with timestamped replay and never suppresses an amount disturbance. Regression tests compute actual `ScaleFlow` values at 10 and 20 Hz, including a rate-only filter case.

The legacy `--settling` mode reads the exported profile's beverage type and applies the same espresso-group exemption as runtime and `--settle-replay`.

## Why the scale-fault branch could go

A clean average is the mean of samples that each passed the over-rise check, so it can never sit more than the limit above the base. The branch rejecting such an average had no reachable input.

## Validation

`shot_eval --settle-replay` over 406 shots, `main`'s rules vs both tests: 29 saved weights change, none of them on a clean shot. Against stop + median drip (1.18 g): 24 closer, 3 within 0.3 g, 2 further (+0.5 g, and +1.1 g for one saved at the base); mean error 6.79 g → 1.01 g. The flow test alone adds 6 to the amount test, among them shot 793, which wobbled inside the amount limit (42.9 → 39.3 g).

## Considered, not done

- **A relative flow margin (k × flow at stop).** Estimator noise does not scale with flow, so an absolute margin is what holds across scales; a relative one would be hair-trigger at low flow.
- **Upstream Visualizer change to keep diagnostics.** Their parser keeps a fixed whitelist for every app; asking the maintainer to store logs or raw streams is his call and would only cover new uploads. Tablet database exports (Mark's) are the faster source of more setups.
- **Limit from the SAW model's expected drip.** The prediction is built from the same flow-at-stop reading that spikes, so it inherits the problem the ceiling solves.
- **Grind check from pour-end weight.** With this change a SAW-stopped shot at a typical target cannot reach the 1.2× gusher ratio from post-stop drift, so it would fix only old records (5 of 1,160 on the tablet, correctable by editing the yield), and it would override a yield the user corrected by hand.
