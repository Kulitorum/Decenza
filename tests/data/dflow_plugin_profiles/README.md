# D-Flow stock profiles — extracted from the plugin

Fixtures for the recipe-editor parity suite (`tests/tst_recipeeditorparity.cpp`). These are
**not** Decenza data. They are the three profiles the D-Flow plugin itself writes, and they are
the oracle the suite checks Decenza against.

## Source

| | |
|---|---|
| Repo | `https://github.com/Damian-AU/D_Flow_Espresso_Profile` |
| Pinned commit | `7f3c9726c4976fe4cc16f41dc9d5429f66416067` |
| Checkout | `de1app/de1plus/plugins/D_Flow_Espresso_Profile` (submodule of `de1app` @ `fe5cf40c`) |

## Why they are extracted rather than copied

D-Flow ships no `.tcl` files. All three profiles are **embedded in Tcl code** and written out at
plugin start (`plugin.tcl`, the `if {$::settings(D_Flow_update) < 2 || ...}` block near the end):

| File | Produced by | Mechanism |
|---|---|---|
| `D-Flow____La_Pavoni.tcl` | `proc write_La_Pavoni_profile` (`plugin.tcl:212`) | `append La_Pavoni_data {...}` then `write_file` — the blob **is** the file, copied verbatim |
| `D-Flow____Q.tcl` | `proc write_Q_profile` (`plugin.tcl:259`) | same |
| `D-Flow____default.tcl` | `proc set_Dflow_default` + `save_profile` | assigns `::settings(<key>) <value>`; de1app's `save_profile` serialises the array as `<key> <value>` lines. Reconstructed by transcribing each assignment in source order, plus the `profile_title` the caller sets immediately before invoking it. |

The first two are verbatim; **the third is reconstructed**, so it is the weaker fixture of the
three. It is faithful to the assignments but does not prove what `save_profile` emits for keys
`set_Dflow_default` never touches. Treat a `default`-only failure as suspect until confirmed
against a real de1app-written `D-Flow____default.tcl`.

## Known oddity in the source data

`D-Flow____default.tcl` carries `espresso_temperature 86.0` while its Filling frame is at 88 °C.
`update_D-Flow` sets `::settings(espresso_temperature)` from `Dflow_filling_temperature`, so the
first edit in the plugin moves it to 88. The 86.0 is what `set_Dflow_default` wrote and has never
been reconciled. This is upstream's inconsistency, not ours — do not "fix" the fixture.

## Regenerating

`tools/extract_dflow_profiles.py`. Re-run on a plugin bump and re-check the pinned commit above;
a transcribed rule can silently go stale against a newer plugin (design D2 / risk "Plugins move").
