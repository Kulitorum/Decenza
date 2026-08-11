# Why

SAW learning keys its history on `(baseProfileName, scaleTypeId)`. The basket is not in the key, so
one learned drip model serves every basket the user owns — and measured drip differs by roughly 2×
between baskets. On this device, D-Flow / Q on a Decent Scale:

| basket | shots | flow at stop (g/s) | actual drip (g) | predicted drip (g) | delta (g) | yield vs 36 g target |
|---|---|---|---|---|---|---|
| Decent 18g Ridged (straight wall) | 1091–1096, 1099 | 1.21–2.18 | 1.00–1.48 (mean 1.32) | 1.24–1.68 | −0.29…+0.15 (mean −0.11) | 35.8–36.4 |
| Graph Stepped 58→46mm | 1098, 1100, 1101 | 0.80–1.48 | 0.52–0.70 (mean 0.63) | 1.31–1.38 | −0.79…−0.62 (mean −0.71) | 35.3–35.5 |

Every Graph-basket shot under-delivered, in one direction, by ~0.7 g — the model was predicting
Decent's drip for a basket that drips half as much. Flow-similarity weighting cannot absorb it: shot
1101 at 0.80 g/s still got 1.31 g predicted, indistinguishable from the 1.48 g/s shots, because the
offset is basket-shaped, not flow-shaped.

The Graph shots also ran finer and longer (grind 15–16.5, 52–58 s) than the Decent shots (grind
8.5–10, 26–30 s), which raises the obvious alternative explanation: a slow-draining puck deposits less
inside the settling gate. Shot 1085 rules it out — Decent basket, 53.3 s, flow 1.58 g/s, drip **1.34
g**, against Graph shot 1098 at 52.1 s, flow 1.48 g/s, drip **0.66 g**. Duration-matched,
flow-matched, same bean and profile, and drip still halves. Grind remains confounded with basket, so
any residual non-basket contribution would have to act through puck retention independently of both
flow and duration.

Evidence limits, stated plainly: the Graph basket has 4 recorded shots, 3 of them with usable SAW
data (1098, 1100, 1101 — 1097 ran away and never triggered a stop), so n=3; one device, one profile. That is enough to
justify **isolating** the key — isolation only asserts the populations may differ, and if the true gap
is smaller than measured the fix still holds, while a gap of zero costs only graduation speed. It
would not be enough to justify a hard-coded per-basket offset, and none is proposed. This is [#847](https://github.com/Kulitorum/Decenza/issues/847)
one level down — profile isolated, basket not — and it runs both ways: shot 1101's own log shows
Graph's 0.52 g accumulating into the `d_flow_q::decent-wifi` batch that the Decent basket reads from.

## Rejected: collapsing the Half Decent Scale transports

An earlier draft of this change also merged the three Half Decent Scale transport ids (`decent`,
`decent-wifi`, `decent-usb`) into one SAW key, on the argument that it is one physical scale whose
sensor lag is dominated by its own sampling cadence rather than the link, with `sensorLag()` already
returning an identical 0.38 s for all three. **Measurement over this device's history refuted it**, so
the transport split stays exactly as it is.

31 shots on D-Flow / Q with the Decent basket, per-shot lag = drip ÷ flow. The two transports
interleave throughout the history (BT at 1032–1050, 1072, 1090, 1091; WiFi at 1068–1099), so this is
not an era comparison:

| group | n | median lag | mean | range |
|---|---|---|---|---|
| BT (`decent`) | 13 | 0.598 s | 0.572 | 0.224–0.824 |
| WiFi (`decent-wifi`) | 18 | 0.840 s | 0.857 | 0.638–1.223 |
| BT, flow 1.30–1.70 g/s | 10 | 0.591 s | 0.546 | 0.224–0.734 |
| WiFi, flow 1.30–1.70 g/s | 7 | 0.848 s | 0.846 | 0.755–0.914 |

Median gap 0.242 s — 0.36 g of drip at 1.5 g/s. Only 16 of 234 cross pairs have BT above WiFi
(Mann-Whitney z = −4.04, p ≈ 5×10⁻⁵), and inside the matched flow band the two ranges do not overlap
at all. Four consecutive-day shots with the same bean, grind 10, 18 g dose, basket and profile give
the tightest control: WiFi 0.879 (1089) and 1.016 (1092) against BT 0.584 (1090) and 0.658 (1091).

Two things worth carrying forward from that result. First, the per-transport split is doing real work
and the physical argument for merging was simply wrong. Second, the WiFi path appears to add ~0.24 s
of latency that the scale's own sampling does not explain — plausibly buffering, and a candidate
defect in the WiFi scale transport in its own right, out of scope here.

# What Changes

- Add the basket to the SAW learning key: `(profile, scale, basket)`, where `scale` remains today's
  per-transport canonical type-id. "No basket" is a distinct key value, matching
  `equipment-package-model`'s package-identity rule.
- Copy existing pre-basket `(profile, scale)` history into a bucket for **every basket that profile
  was actually pulled with**, per the recent shot history, once — so each combination that exists
  keeps predicting exactly what the single shared model predicted before the upgrade and then
  diverges as it earns its own medians. Untried combinations are not seeded. The read path keeps
  today's tier order; no permanent fallback tier is added.
- Report the basket in the Calibration tab's model-source suffix and in the per-shot
  `[SAW][Learning] Model:` / `Accuracy:` log lines.

One flag-guarded QSettings copy on first launch, keyed off a bounded 500-shot query for the distinct
(profile, basket) pairs actually pulled. Not breaking — no schema version, no API removal, and the pre-basket keys are left in
place so an older build still reads them on rollback.

## Assumptions

- **Portafilter/spout is out of scope.** The physically responsible geometry is basket plus spout,
  and #847 was open-vs-double spout. Nearly all Decenza users run bottomless, so spout is not
  modelled and no key field is reserved for it.
- Basket identity for keying is the basket item's `brand` + `model`, the same pair
  `equipment-package-model` uses for package identity. Registry specs (wall profile, dose range) are
  not part of the key.
- Grinder, burrs, RPM and puck-prep stay out of the key: they act through puck permeability, which
  already reaches the model as flow at stop. Paper filter is the only plausible non-basket
  contributor to post-stop drainage and is well under the resolution of the available data.

# Capabilities

## New Capabilities

None.

## Modified Capabilities

- `stop-at-weight-learning`: the per-pair key gains a basket dimension; pre-basket history is copied
  once into the profile-and-basket combinations actually pulled; the per-shot diagnostic lines gain
  the basket.

`scale-type-identity` is deliberately **not** modified — see "Rejected" above.

# Impact

- `src/core/settings_calibration.{h,cpp}` — `sawPairKey` gains a basket argument, the one-time
  `seedSawBucketsFromPreBasketKeys()`, bootstrap contributor set widened across baskets (skipping
  inherited medians), and every public per-pair entry point
  (`sawLearnedLagFor`, `getExpectedDripFor`, `sawLearningEntriesFor`, `sawModelSource`,
  `resetSawLearningForProfile`, `addSawLearningPoint`).
- `src/main.cpp` — pass the active package's basket into the WeightProcessor snapshot and the
  `sawLearningComplete` handler; extend the two per-shot log lines.
- `src/core/settings_dye.cpp` — source of the active basket brand/model at snapshot time.
- `src/history/shothistorystorage.{h,cpp}` — new bounded `requestRecentProfileBasketPairs()` query.
- `src/controllers/maincontroller.cpp` — requests it at startup, maps shot profile TITLES to
  filenames via `ProfileManager::titleToFilename()`, and feeds the seed.
- `src/mcp/mcptools_control.cpp` — `reset_saw_learning_for_profile` semantics across baskets.
- `qml/pages/settings/SettingsCalibrationTab.qml` — model-source suffix and reset-scope wording.
- `tests/tst_saw_settings.cpp` — extended in place (no new test file).
- `docs/CLAUDE_MD/SAW_LEARNING.md` — storage schema, key shape, read-path chain, migration and its
  timing, logging table.
