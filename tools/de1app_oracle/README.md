# de1app oracle

Runs **de1app's own frame builders** over the stock `.tcl` profiles and diffs the
result against Decenza's shipped built-ins.

```bash
python3 tools/de1app_oracle/compare_builtins.py ~/Development/GitHub/de1app
python3 tools/de1app_oracle/compare_builtins.py ~/Development/GitHub/de1app --verbose  # also list inactive-axis diffs
```

Exit status is `1` when any profile would brew differently, so it can gate a sync.

## Why this exists, when `profile_sync` already compares against the `.tcl`

**The `.tcl` is de1app's input, not its output.** For a simple profile
(`settings_2a`/`2b`) de1app discards the stored `advanced_shot` and rebuilds the
frames from the scalars, so the file's frames are not what it brews. Comparing
against the file therefore cannot answer the only question that matters — *does
this profile make the same coffee in both apps?*

`de1app_frames.tcl` answers it by `source`ing de1app's real `profile.tcl` and
calling `pressure_to_advanced_list` / `flow_to_advanced_list` /
`settings_to_advanced_list` on it.

**The builders are de1app's own; the three-way dispatch is mirrored.** The script
does not call `::profile::sync_from_legacy`, because that also runs the huddle/
JSON converter this tool has no use for — so the `switch` at the bottom of
`de1app_frames.tcl` is a copy of `profile.tcl:465-472` and can drift from it.
That is the one piece to re-check when de1app changes. Everything that computes
a frame is theirs.

It earned its place immediately: on first run it found three classes of
divergence the entire C++ suite had missed, across four profiles — two Decenza
brewed at 88 °C that de1app brews at 92 °C and 94 °C, and two where we omitted a
whole preinfusion frame — plus an inactive-axis value we invented on 19 built-ins.

## When to run it

- After pulling a new de1app revision, or when de1app adds/changes profiles.
- Before `profile_sync --sync`, and again after, to confirm the sync converged.
- When touching `generatePressureProfileFrames` / `generateFlowProfileFrames`,
  frame serialization, or the Tcl reader.

## Reading the output

Differences are split by whether the DE1 acts on them:

| Bucket | Meaning |
|---|---|
| `SHOT` | The machine does something different. A real portability break. |
| `INACTIVE` | The axis the frame's pump does not use (`flow` on a pressure frame). The DE1 ignores it, so the coffee is identical, but a byte-level reader still sees a value de1app never wrote. |

An empty value in `de1app_frames.tcl` output means de1app did not set that key at
all. That is information, not noise: de1app writes no `flow` on a pressure frame,
so anything we put there is ours, not theirs.

## Requirements and caveats

- `tclsh` (any Tcl 8.6+/9.x; developed against 9.0.4). Not a build or test
  dependency — this is a developer tool, deliberately not wired into CMake or
  ctest, so a machine without Tcl can still build and run the suite.
- A local de1app checkout. Its path is an argument, never hardcoded.
- `de1app_frames.tcl` supplies the few globals `profile.tcl` needs (`ifexists`,
  `msg`, stubbed `package require`) and extracts `profile_vars` out of
  `vars.tcl` at runtime rather than copying it, so it tracks the checkout.
- It also replicates the preset block from `convert_all_legacy_to_v2`
  (`maximum_flow`/`maximum_pressure` → 0, "Disable limits by default") **before**
  overlaying the profile. This is load-bearing: without it de1app's own builder
  aborts on de1app's own shipped profiles.

## Related

- `tools/profile_sync.cpp` — compares profile-level **scalars** and frames against
  the `.tcl`, and is the thing that writes `resources/profiles/`.
- `tests/tst_recipegenerator.cpp` — checks the generators against de1app's
  formulas. This tool checks them against de1app's actual output; they are
  complementary, and neither can use the stored frames as an oracle
  (see `docs/CLAUDE_MD/RECIPE_PROFILES.md`).
