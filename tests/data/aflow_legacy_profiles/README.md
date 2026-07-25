# A-Flow legacy 6-frame profile — the OLD layout, not the oracle

One fixture, for the legacy branch of `set_profile_index` (`A_Flow/code.tcl:171-190`) and the
upgrade path in `update_A-Flow` that inserts `Pre Fill`, `2nd Fill` and `Pause`.

## Read this before using it for anything else

This file is **de1app's stale snapshot**, copied from `de1plus/profiles/`. It is *not* what the
A-Flow plugin ships. The plugin's own `profiles/` directory carries all five profiles at **9**
frames; de1app's distribution copy carries four at **6**, added in de1app commit `80eb34cc`
(2025-09-03) and never refreshed. `check_profiles_exist` only copies a profile when the file is
absent, so the stale copy wins forever and cannot self-correct. Upstream: de1app issue #350.

So:

- **As the current-behaviour oracle it is wrong.** Never compare generated output against it.
  Those fixtures live in `tests/data/de1app_profiles/` and are byte-identical to the plugin's.
- **As the legacy-layout case it is exactly right**, and is genuinely in the field — anyone whose
  de1app installed these before the plugin updated still has them. `set_profile_index` exists
  precisely to keep reading them.

The frame roles it exercises (legacy branch):

| index | role | frame name |
|---|---|---|
| 0 | `filling` | Fill |
| 1 | `soaking` | Infuse |
| 2 | `ramp_up` | Pressure Up |
| 3 | `ramp_down` | Pressure Decline |
| 4 | `pouring_start` | Flow Start |
| 5 | `pouring` | Flow Extraction |

Note what is *absent*: `pre_filling`, `2nd_fill` and `pause`. Reading this layout with the 9-frame
indices shifts every role by one and silently reads `2nd Fill` where `soaking` belongs — which is
the class of bug AF-5 already is in the 9-frame path.
