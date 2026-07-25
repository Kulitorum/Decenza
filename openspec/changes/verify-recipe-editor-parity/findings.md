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

## Does it all make a whole? Yes — and that is what makes the findings findings

Read against de1app's shot-execution path, the plugin and its profiles are one coherent design,
not code plus stale data. Three facts settle it:

| frame field | where it goes | source |
|---|---|---|
| `weight` | app-side: `profile_target`, and exceeding it calls `start_next_step` | `device_scale.tcl:1210,1254` |
| `volume` | **to the machine**, packed unconditionally as `MaxVol` | `binary.tcl:967` |
| `exit_pressure_over` | to the machine **only when `exit_if 1`**; otherwise `TriggerVal 0` | `binary.tcl:933,955` |

And the firmware's own contract for `MaxVol`, quoted from de1app:

> `U10P0  MaxVol;  // Exit current frame if the volume/weight exceeds this value. 0 means ignore`

So the division of labour is clean:

1. **The editor owns exactly eight parameters** — fill temperature; soak seconds/pressure/volume/
   weight; pour flow/pressure/temperature. That is the whole of `prep`, and the whole of what
   `update_D-Flow` writes back.
2. **Everything else is per-profile character**, baked in by the author: the fill volume cap, the
   fill weight-skip, the pour volume cap, the fill exit pressure.
3. **`update_D-Flow` mutates in place precisely so editing the eight never disturbs the rest.**
   That is not an implementation detail — it is the mechanism that lets one editor drive three
   profiles with different machine personalities.

The three stock profiles then differ in exactly the non-editor fields, deliberately:

| | fill vol | fill wt | pour vol |
|---|---|---|---|
| D-Flow / default | 100 | 5.00 (skip at 5 g) | 0 (uncapped) |
| D-Flow / Q | 60 | 0.00 (no skip) | 100 |
| D-Flow / La Pavoni | 60 | 0.00 (no skip) | 100 |

Q and La Pavoni are lever simulations: a 60 mL fill cap, a 100 mL pour cap, no weight-skip.
Default is the everyday profile: uncapped pour, 5 g fill skip. Coherent, and each choice is
visible in what reaches the machine.

**The whole holds provided the editor preserves what it does not own.** That is the contract
Decenza breaks — and the shape of the break is specific:

> **Every constant Decenza hardcodes is `D-Flow / default`'s value.**
> fill volume 100, fill weight 5.0, pour volume 0 — default's, all three.

The D-Flow generator was written by reading one profile and promoting its literals to universal
constants. Q and La Pavoni are the two that differ, and they are exactly the two that get
overwritten. That is one root cause with three symptoms, not three bugs.

### Two corrections to the first pass of this document

- **DF-3 is not "stale upstream data".** I called it that before reading the execution path. Under
  the intentional reading it is the opposite: the shipped exit pressures are authored values, and
  `update_D-Flow`'s derived rule is the *fallback* for when the user changes soak pressure — it
  recomputes because it must emit something, not because the authored value was wrong. de1app
  moving default's 1.5 → 2.1 on first edit is an accepted consequence of editing. Nothing to
  report upstream; **DF-3 is retired as a defect**, and Decenza's behaviour is correct.
- **DF-1 and DF-5 are not "low severity, inert".** `volume` is packed into `MaxVol` and sent to
  the DE1 unconditionally. They change what the machine does. DF-5 in particular removes a cap
  rather than shifting one — see below.

---

## DF-1 — `filling(volume)` forced to 100 · Decenza defect · reaches the machine

`RecipeGenerator::createFillFrame` hardcodes `frame.volume = 100.0`. `update_D-Flow` never writes
`filling(volume)`, so the plugin preserves whatever the profile carried. **D-Flow / Q** and
**D-Flow / La Pavoni** both ship `60`.

Effect: opening either in Decenza and saving relaxes the fill frame's `MaxVol` from 60 mL to
100 mL. Both values are non-zero, so the cap stays armed — it just moves. In practice the fill
exits on pressure well before either, so the practical impact is small; but this is a value sent
to the DE1, not stored metadata, and it is a silent edit to a profile the plugin leaves alone.

## DF-2 — `filling(weight)` forced to 5 g · Decenza defect · **shot-affecting**

`createFillFrame` hardcodes `frame.exitWeight = 5.0`, commented *"matches de1app default: weight
5.00"*. That value is only **D-Flow / default**'s. **Q** and **La Pavoni** both ship `weight 0.00`
— meaning *no* weight exit — and `update_D-Flow` never writes the field.

Effect: Decenza imposes a 5 g app-side exit on a fill step whose author asked for none, cutting
the fill short. This is the most serious D-Flow finding: it changes what the machine does, on two
of the three shipped profiles, without the user touching anything but Save.

The comment is the tell — a value observed in one profile was generalised into a constant.

## DF-3 — `filling(exit_pressure_over)` recomputed · **RETIRED — not a defect**

Decenza applies `update_D-Flow`'s derived rule faithfully. Two of the three stock profiles carry
values that rule would not produce:

| Profile | soak pressure | shipped exit | rule gives | |
|---|---|---|---|---|
| D-Flow / default | 3.0 | 1.5 | 2.1 | differs |
| D-Flow / Q | 6.0 | 3.0 | 3.6 | differs |
| D-Flow / La Pavoni | 1.2 | 1.2 | 1.2 | **matches** |

La Pavoni matching is what makes this diagnosable: a mis-transcribed rule would be wrong in all
three, not two.

Treating the profiles as intentional resolves it: the shipped exit pressures are **authored**, and
the derived rule is the fallback the editor applies when the user changes soak pressure — it has
to emit something, and half-the-soak-plus-0.6 is a reasonable something. de1app moving default's
1.5 → 2.1 on first edit is an accepted consequence of editing, not a bug in either place.

**No action, either side.** Decenza's behaviour here is correct. Kept in the suite's
known-divergent list so it does not read as an unexplained gap.

## DF-4 — `soaking(exit_pressure_over)` rewritten · Decenza defect · inert at runtime

`createInfuseFrame` sets `frame.exitPressureOver = recipe.infusePressure`. `update_D-Flow` never
writes `soaking(exit_pressure_over)`; all three stock profiles ship `3.0` regardless of soak
pressure. Decenza rewrites it — 6.0 on Q, 1.2 on La Pavoni.

**Confirmed inert at runtime**, and now for a cited reason rather than an assumption: the soak
frame carries `exit_if 0`, and `binary.tcl:955` sets `TriggerVal 0` whenever `exit_if != 1`. The
value never reaches the machine. It is still a stored difference that round-trips into files other
apps read, and "inert in today's firmware path" is not "inert".

## DF-5 — `pouring(volume)` forced to 0 · Decenza defect · **removes a cap**

`createPourFrame` sets `frame.volume = 0.0` — again `D-Flow / default`'s value. `update_D-Flow`
never writes `pouring(volume)`; Q and La Pavoni ship `100`.

This is not the same as DF-1's "cap moves". Per the firmware contract, **`0` means ignore**. So
Q and La Pavoni intend the pour frame to end at 100 mL, and Decenza sends 0 — deleting the limit
outright. On a lever-simulation profile with a 127-second pour frame, the volume cap is the
backstop; the shot is otherwise ended by weight, and a user brewing without a scale has nothing
left holding it.

Ranking this second only to DF-2 among the D-Flow findings.

---

## Root cause, common to DF-1/2/5 (and DF-4)

The plugin **mutates frames in place** — read the current `advanced_shot`, overwrite a named list
of fields, write it back — so every field outside that list survives untouched. That is the
mechanism letting one editor drive three profiles with different machine personalities.

Decenza **constructs frames from constants**, so every field outside the eight editor parameters
gets a literal. And the literals are not arbitrary: **all three are `D-Flow / default`'s values.**
The generator was written by reading one profile and generalising it. Q and La Pavoni are the two
that differ from default, and they are exactly the two that get overwritten.

One root cause, three runtime symptoms. Fixing the constants closes today's cases; adopting the
plugin's mutate-in-place shape closes the class — and is what keeps a fourth profile from
arriving with a fourth field nobody thought to preserve. That is a design decision, deliberately
left to a follow-up (design D7) rather than made inside a verification change.

**Severity order for repair:** DF-2 (imposes a 5 g fill skip that ends the fill early),
DF-5 (deletes the pour volume cap), DF-1 (relaxes the fill cap), DF-4 (stored only).

---

## Not yet assessed

§3–5 (A-Flow extraction, generation, frame layouts), §6 (inheritance), §7 (Decenza-only
parameters — `fillTimeout` / `fillPressure` / `fillFlow` / `infuseEnabled`), §8 (editor coverage).
A-Flow has three structural toggles and a 9-vs-6-frame layout split, so it has more surface than
D-Flow, and the in-place-vs-constants gap above predicts more of the same class there.
