# Findings

Divergences between Decenza and the upstream plugins, found by
`tests/tst_recipeeditorparity.cpp`. Each is a live expected-failure in the suite, so the gate
records reality rather than a weakened assertion (design D7).

Oracle: `D_Flow_Espresso_Profile` @ `7f3c9726` (v3.1), `A_Flow` @ `e1a4d871` (v2.0-beta.2-2),
both verified at upstream HEAD on 2026-07-25.

**Status: D-Flow (§2) complete. A-Flow (§3–5) not yet run.**

---

## What passed

Worth stating, because it bounds the problem. On all three stock D-Flow profiles:

- **Extraction matches `prep` exactly** — every one of the eight parameters, including the two
  that are easy to get wrong: pour pressure read from `pouring(max_flow_or_pressure)` rather than
  the vestigial `pouring(pressure)`, and soak time via `round_to_one_digits`.
- **Generation matches `update_D-Flow`** on every field that proc writes.
- **The derived fill-pressure rule is exactly right** across all six branches, including the
  `< 2.8` threshold, the `soak/2 + 0.6` formula, and the 1.2 floor.
- **The D-Flow/A-Flow temperature asymmetry is correct** — D-Flow's soak frame takes the *pour*
  temperature, and Decenza does that.

So the core of the D-Flow implementation is faithful. Every finding below is a field Decenza
writes that the plugin does not.

---

## DF-1 — `filling(volume)` forced to 100 · Decenza defect · low severity

`RecipeGenerator::createFillFrame` hardcodes `frame.volume = 100.0`. `update_D-Flow` never writes
`filling(volume)`, so the plugin preserves whatever the profile carried. **D-Flow / Q** and
**D-Flow / La Pavoni** both ship `60`.

Effect: opening either in Decenza and saving changes the fill frame's volumetric limit 60 → 100.
Low severity because the fill step exits on pressure long before either limit is reached — but it
is a silent, unrequested edit to a profile the plugin would have left alone.

## DF-2 — `filling(weight)` forced to 5 g · Decenza defect · **shot-affecting**

`createFillFrame` hardcodes `frame.exitWeight = 5.0`, commented *"matches de1app default: weight
5.00"*. That value is only **D-Flow / default**'s. **Q** and **La Pavoni** both ship `weight 0.00`
— meaning *no* weight exit — and `update_D-Flow` never writes the field.

Effect: Decenza imposes a 5 g app-side exit on a fill step whose author asked for none, cutting
the fill short. This is the most serious D-Flow finding: it changes what the machine does, on two
of the three shipped profiles, without the user touching anything but Save.

The comment is the tell — a value observed in one profile was generalised into a constant.

## DF-3 — `filling(exit_pressure_over)` recomputed · **UPSTREAM, not a Decenza defect**

Decenza applies `update_D-Flow`'s derived rule faithfully. Two of the plugin's own three stock
profiles carry values that rule would not produce:

| Profile | soak pressure | shipped exit | rule gives | |
|---|---|---|---|---|
| D-Flow / default | 3.0 | 1.5 | 2.1 | differs |
| D-Flow / Q | 6.0 | 3.0 | 3.6 | differs |
| D-Flow / La Pavoni | 1.2 | 1.2 | 1.2 | **matches** |

La Pavoni matching is what makes this diagnosable: if the rule were mis-transcribed it would be
wrong in all three, not two. The stock blobs are written literally by `write_*_profile` /
`set_Dflow_default` and are only recomputed when a user edits — so **de1app itself is not a
round-trip fixed point on these two profiles either** and would make the same change on first
edit.

No action for Decenza. Worth reporting upstream: the shipped data contradicts the plugin's own
recompute rule.

## DF-4 — `soaking(exit_pressure_over)` rewritten · Decenza defect · low severity

`createInfuseFrame` sets `frame.exitPressureOver = recipe.infusePressure`. `update_D-Flow` never
writes `soaking(exit_pressure_over)`; all three stock profiles ship `3.0` regardless of their soak
pressure. Decenza rewrites it to the soak pressure — 6.0 on Q, 1.2 on La Pavoni.

Low severity: the frame has `exit_if 0`, so the field is inert (Decenza's own code calls these
"dead exit fields stored for de1app compatibility"). But it is stored, it round-trips into files
other apps read, and "inert today" is not "inert".

## DF-5 — `pouring(volume)` forced to 0 · Decenza defect · low severity

`createPourFrame` sets `frame.volume = 0.0`. `update_D-Flow` never writes `pouring(volume)`; Q and
La Pavoni ship `100`. Same class as DF-1.

---

## Root cause, common to DF-1/2/4/5

The plugins **mutate frames in place** — read the current `advanced_shot`, overwrite a named list
of fields, write it back — so every field outside that list survives untouched. Decenza
**constructs frames from constants**, so every field outside the recipe's own parameters gets a
literal, and any literal that does not match what the profile carried is a silent edit.

This is architectural, not four typos. Fixing the four constants closes today's cases; adopting
the plugins' mutate-in-place shape would close the class. That is a design decision, deliberately
left to a follow-up (design D7) rather than made inside a verification change.

---

## Not yet assessed

§3–5 (A-Flow extraction, generation, frame layouts), §6 (inheritance), §7 (Decenza-only
parameters — `fillTimeout` / `fillPressure` / `fillFlow` / `infuseEnabled`), §8 (editor coverage).
A-Flow has three structural toggles and a 9-vs-6-frame layout split, so it has more surface than
D-Flow, and the in-place-vs-constants gap above predicts more of the same class there.
