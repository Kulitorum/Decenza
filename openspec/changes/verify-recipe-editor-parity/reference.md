# Plugin reference — transcribed rules

The oracle for `tests/tst_recipeeditorparity.cpp`. Every rule below is transcribed from plugin
source with its line cited. **Decenza is not consulted anywhere in this document** — where the two
disagree, the plugin is right by definition and the difference is a finding (design D1).

Pinned at: `A_Flow` `e1a4d871`, `D_Flow_Espresso_Profile` `7f3c9726`, `de1app` `fe5cf40c`.
Re-verify on any submodule bump — a transcribed rule goes stale silently.

**Currency verified 2026-07-25.** All three fetched and compared against their upstream default
branches: 0 commits behind in every case. A-Flow is at `v2.0-beta.2-2-ge1a4d87`, D-Flow at `v3.1`.

One wrinkle worth knowing: `git submodule status` reports A_Flow with a `+` prefix, meaning the
commit checked out locally is **not** the one de1app's index pins — de1app pins an older A-Flow
than the plugin's own HEAD. Our checkout is the newer, which is what we want (and is consistent
with de1app #350, where the distribution lags the plugin). D-Flow matches de1app's pin exactly.
So "latest de1app" and "latest A-Flow" are not the same thing, and this suite tracks the plugin.

---

## D-Flow — `D_Flow_Espresso_Profile/plugin.tcl`

Three frames, fixed indices, no layout variants. Guarded by `[string range $::settings(profile_title) 0 7] == "D-Flow /"` (`:196`).

| index | role |
|---|---|
| 0 | `filling` |
| 1 | `soaking` |
| 2 | `pouring` |

### Read — `proc prep` (`:194`–`:209`)

| Parameter | Source frame field |
|---|---|
| `Dflow_filling_temperature` | `filling(temperature)` |
| `Dflow_soaking_seconds` | `round_to_one_digits soaking(seconds)` |
| `Dflow_soaking_pressure` | `soaking(pressure)` |
| `Dflow_soaking_volume` | `soaking(volume)` |
| `Dflow_soaking_weight` | `soaking(weight)` |
| `Dflow_pouring_flow` | `round_to_one_digits pouring(flow)` |
| `Dflow_pouring_pressure` | **`pouring(max_flow_or_pressure)`** — the limiter, *not* `pouring(pressure)` |
| `Dflow_pouring_temperature` | `pouring(temperature)` |

Eight parameters. There is no fill flow, fill time, fill pressure, or infuse-enable parameter.

### Write — `proc update_D-Flow` (`:332`–`:360`)

```
::settings(espresso_temperature) = Dflow_filling_temperature      ; from FILL temp
filling(temperature)             = Dflow_filling_temperature
filling(pressure)                = Dflow_soaking_pressure         ; NOT independent
filling(exit_pressure_over)      = derived, see below
soaking(temperature)             = Dflow_pouring_temperature      ; from POUR temp
soaking(pressure)                = Dflow_soaking_pressure
soaking(seconds)                 = Dflow_soaking_seconds
soaking(volume)                  = Dflow_soaking_volume
soaking(weight)                  = Dflow_soaking_weight
pouring(temperature)             = Dflow_pouring_temperature
pouring(flow)                    = Dflow_pouring_flow
pouring(max_flow_or_pressure)    = Dflow_pouring_pressure
```

**Derived fill exit pressure** (`:338`–`:344`):

```
if soak < 2.8:  exit = soak
else:           exit = round_to_one_digits(soak / 2 + 0.6)
if exit < 1.2:  exit = 1.2
```

**Fields `update_D-Flow` never writes** — a Decenza save must leave these alone:
`filling(seconds)`, `filling(flow)`, `filling(volume)`, `filling(weight)`, `filling(max_flow_or_pressure)`,
`pouring(pressure)`, `pouring(seconds)`, every `exit_*` except `filling(exit_pressure_over)`,
and every `transition`/`pump`/`sensor`/`name`.

The proc mutates the three frames **in place** (`array set` from the current `advanced_shot`, then
`lappend` back). It does not construct frames from constants.

### Temperature asymmetry, stated plainly

`filling(temperature)` ← **fill** temp; `soaking(temperature)` ← **pour** temp. A-Flow differs (below).

---

## A-Flow — `A_Flow/code.tcl`

Guarded by `[string range $::settings(profile_title) 0 7] == "A-Flow /"` (`:196`).

### Layout — `proc set_profile_index` (`:171`–`:190`)

Selected on `[llength $::settings(advanced_shot)] > 8`:

| role | 9-frame | 6-frame (legacy) |
|---|---|---|
| `pre_filling` | 0 | — |
| `filling` | 1 | 0 |
| `soaking` | 2 | 1 |
| `2nd_fill` | 3 | — |
| `pause` | 4 | — |
| `ramp_up` | 5 | 2 |
| `ramp_down` | 6 | 3 |
| `pouring_start` | 7 | 4 |
| `pouring` | 8 | 5 |

### Read — `proc prep` (`:193`–`:238`)

| Parameter | Source |
|---|---|
| `Aflow_filling_temperature` | `filling(temperature)` |
| `Aflow_filling_flow` | `filling(flow)` |
| `Aflow_soaking_seconds` | `round_to_one_digits soaking(seconds)` |
| `Aflow_soaking_pressure` | `soaking(pressure)` |
| `Aflow_soaking_volume` | `soaking(volume)` |
| `Aflow_soaking_weight` | `soaking(weight)` |
| `Aflow_ramp_updown_seconds` | `round_to_integer(ramp_up(seconds) + ramp_down(seconds))` — the **sum** |
| `Aflow_pouring_flow` | `round_to_one_digits pouring_start(flow)` — from **Flow Start**, not extraction |
| `Aflow_pouring_pressure` | `ramp_up(pressure)` |
| `Aflow_pouring_temperature` | `ramp_up(temperature)` |
| `Aflow_ramp_down_pressure` | `ramp_down(pressure)` |

**Toggles, derived from frame structure — stored nowhere:**

```
ramp_down_enabled  = ramp_down(seconds) > 0
flow_extraction_up = pouring(flow) > Aflow_pouring_flow
2nd_fill_step      = llength(advanced_shot) > 8 && pause(seconds) > 0
```

### Write — `proc update_A-Flow` (`:242`–`:300`)

```
filling(temperature)          = Aflow_filling_temperature
soaking(temperature)          = Aflow_filling_temperature    ; FILL temp — differs from D-Flow
soaking(pressure/seconds/volume/weight) = Aflow_soaking_*
ramp_up(temperature)          = Aflow_pouring_temperature
ramp_up(pressure)             = Aflow_pouring_pressure
ramp_down(temperature)        = Aflow_pouring_temperature
ramp_down(exit_flow_under)    = round_to_one_digits(Aflow_pouring_flow + 0.1)
pouring_start(temperature)    = Aflow_pouring_temperature
pouring_start(flow)           = Aflow_pouring_flow
pouring(temperature)          = Aflow_pouring_temperature
pouring(max_flow_or_pressure) = Aflow_pouring_pressure
```

**Ramp split** (`:262`–`:270`):

```
if ramp_down_enabled:
    ramp_up(seconds)        = round_to_integer(ramp_updown / 2)
    ramp_down(seconds)      = round_to_integer(ramp_updown / 2 + (ramp_updown % 2 ? 1 : 0))
    ramp_up(exit_flow_over) = round_to_one_digits(pouring_flow * 2)
else:
    ramp_up(seconds)        = round_to_integer(ramp_updown)
    ramp_down(seconds)      = 0
    ramp_up(exit_flow_over) = round_to_one_digits(pouring_flow)
```

Note `%` requires an integer, so `Aflow_ramp_updown_seconds` is integral. Odd values give the
**extra second to the decline**, not the ramp-up.

**Flow Start activation** (`:274`–`:283`) — keyed on `ramp_up(seconds)`, i.e. the *post-split* value:

```
if ramp_up(seconds) < 1:
    pouring_start(seconds)        = 10
    pouring_start(exit_flow_over) = round_to_one_digits(pouring_flow - 0.1)
    pouring_start(exit_type)      = flow_over
    pouring_start(exit_if)        = 1
else:
    pouring_start(seconds)        = 0
```

**Extraction flow** (`:286`–`:291`):

```
pouring(flow) = flow_extraction_up ? round_to_one_digits(pouring_flow * 2) : 0
```

**Fields `update_A-Flow` never writes:** `filling(seconds)`, `filling(flow)`, `filling(pressure)`,
`filling(exit_*)`, `soaking(exit_*)`, `ramp_up(flow)`, `ramp_down(pressure)`, `pouring(pressure)`,
`pouring(seconds)`, and the whole of `pre_filling`, `2nd_fill`, `pause` on an existing 9-frame
profile. Note `Aflow_filling_flow` is **read** by `prep` but never written back by `update` — and
`ramp_down_pressure` likewise.

**Legacy upgrade** (`:295`+): when `llength <= 8`, the proc synthesises `pre_filling`, `2nd_fill`
and `pause` from literals and inserts them, producing the 9-frame layout.

---

## Divergences to check, stated as expectations

1. `soaking(temperature)`: A-Flow ← fill temp, D-Flow ← pour temp. Not interchangeable.
2. `pouring_pressure`: D-Flow ← `pouring(max_flow_or_pressure)`, A-Flow ← `ramp_up(pressure)`.
3. `pouring_flow`: D-Flow ← `pouring(flow)`, A-Flow ← `pouring_start(flow)`.
4. Neither plugin exposes a fill *time* or fill *pressure* parameter. D-Flow derives fill pressure
   from the soak; A-Flow does not write `filling(pressure)` at all.
5. `Aflow_filling_flow` and `Aflow_ramp_down_pressure` are read-only round-trip carriers — `prep`
   loads them, `update` never writes them back.
