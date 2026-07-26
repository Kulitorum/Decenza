## Context

`topLevelAssignments` in `de1apptclfields.cpp` is the single definition of "a top-level Tcl
assignment", shared by `extractValue` and `assignedTclKeys`. For a value that is neither braced
nor quoted it ends with:

```cpp
out.append({key, rest.section(QRegularExpression("[ \t]"), 0, 0)});
```

First token only. That matches Tcl semantics: a profile file is read with `array set`, which
treats the whole file as a flat key/value list, so `profile_title D-Flow / Q` yields
`profile_title` → `D-Flow` plus a stray `/` → `Q`. Verified directly with `tclsh` against the
downloaded file.

de1app never trips this, because it braces every multi-word value — all 88 shipped profiles do,
and an audit of the corpus finds **zero** bare multi-word values at brace depth 0. Visualizer's
`.tcl` renderer does not, so every multi-word title from that export truncates. Its JSON renderer
gets the title right, but the JSON path is not a reliable escape: the importer requests
`?format=json` and still carries a live TCL branch, with the comment *"some Visualizer profiles
return TCL instead of JSON."*

Consequences of the truncation, measured on the real import (`downloaded/d_flow.json`): title
`D-Flow`, `category` null so it leaves the D-Flow group, filename `d_flow.json` which the next
Visualizer D-Flow download would overwrite. Frames, targets and preinfuse count were all correct.

Separately, `profile_grinder_dose_weight` / `profile_grinder_setting` are in de1app's
`profile_vars` list but appear in neither Decenza's scalar table nor its known-ignored list. Only
de1app's Streamline skin populates them (`skin.tcl:2550-2556`), which is why no shipped profile
has one and the drift gate has never flagged them.

## Goals / Non-Goals

**Goals:**

- Recover the author's title from a malformed but widespread export shape.
- Read de1app's per-profile dose instead of discarding it.
- Close the drift-gate hole for both grinder keys.
- Retire a change whose premise is disproved.

**Non-Goals:**

- Changing the bare-value rule for anything that can reach a frame or a machine value.
- Any new dose UI. Recipes own dose going forward; profiles keep assuming the 18 g default.
- Emitting `profile_grinder_dose_weight` on export — Decenza writes no Tcl at all, so there is
  no export side to this.
- Mapping `profile_grinder_setting` to anything. Grind settings live on equipment and recipes.
- Fixing Visualizer. That is an upstream issue, filed alongside but not blocking.

## Decisions

**Scope the rest-of-line rule to three prose keys, not to all bare values and not to all
non-numeric ones.** A blanket rule would also change numeric parsing: `maximum_flow 2.5 9`
currently reads `2.5` and would become the malformed string `"2.5 9"`, flipping a silently-wrong
value into a hard validation failure. That may even be an improvement, but it is a behaviour
change to values that reach the machine, for a naming bug. Confining the rule to three prose keys
means nothing that touches a frame, a machine value or a classification can move.

An earlier draft of this list also carried `beverage_type`, `profile_language` and
`original_profile_title`. All three are wrong. `beverage_type` is an **enum**, written bare
across the corpus in eight values (`espresso` x44, `tea_portafilter` x11, `calibrate` x5,
`cleaning` x3, `pourover` x3, `filter` x2, `manual`, `tea`) — reading a malformed line whole
yields an unmatchable string and silently drops a classification that drives tea/pourover
handling and travels on to Visualizer and reaprime. That is the one case where the divergence is
strictly WORSE than truncation, and it was inside our own list. `profile_language` is a code, same
argument. `original_profile_title` is modelled nowhere in Decenza and is not in `nonScalarTclKeys()`
either, so including it would put an unhandled key into `uncoveredTclKeys()`.

*Alternative considered:* detect the truncation heuristically — e.g. notice a stray single-
character key like `/` and stitch it back. Rejected as unreliable and unreadable.

**Accept the divergence from de1app on titles.** de1app reads `D-Flow`; we will read
`D-Flow / Q`. This is a deliberate, documented departure from the "de1app is the oracle" rule
that governed the parity work — justified because the oracle rule exists to protect *what the
machine brews*, and here matching de1app produces a strictly worse result (a lost category, a
filename collision, and in de1app's own case a lost editor) with no compensating benefit. It also
self-corrects the ecosystem: Decenza's own writer braces properly, so re-exporting repairs the
file for everyone downstream.

**Gate `has_recommended_dose` on a value greater than zero, in `loadFromTclString` rather than in
the field table.** de1app writes the key on every save from Streamline, so a `0` means "not set"
rather than "a dose of zero" — the same reading the 8 shipped profiles carrying
`grinder_dose_weight 0` support. `readScalar` returns a bare double with no mechanism for setting
a companion boolean, so the conditional has to live in `Profile::loadFromTclString`.

**The new table row must leave its absent-value substitute unset.** `compareScalars` walks the
same table and IS the built-in drift gate. Give the row a `0` fallback and every one of the 88
corpus profiles — none of which carries the key — yields `0` compared against each built-in's
`recommended_dose: 18.0`, failing the gate on eight files. Unset is both correct and required.

**List `profile_grinder_setting` as ignored rather than mapping it.** Decenza models grind
settings on equipment and recipes, not on the profile; mapping a per-profile grind string onto
either would invent an association de1app does not make. Listing it with evidence is what the
existing entries for `grinder_model` / `grinder_setting` already do.

**Withdraw `preserve-recipe-visualizer-roundtrip` rather than rewrite it.** Its Non-Goal states
that frame→recipe reconstruction "cannot recover A-Flow toggles or distinguish editor variants;
explicitly rejected." Both plugins do exactly that in `proc prep` on every load, all three A-Flow
toggles derive from frame structure in three lines, editor variant travels in the title prefix,
and the edit matrix sits at 0 divergences across 99 cases. What remains of it — the
`target_volume_count_start` handling and the Visualizer download-format choice — either shipped
already or stands alone and can be re-proposed on its own merits.

## Risks / Trade-offs

**A bare value containing a trailing token that is really the next key** → e.g. two assignments
on one line. `topLevelAssignments` only ever captured the first key per line, so the second was
already lost; the rest-of-line rule makes the first value wrong instead of the second value
missing. No real file does this — de1app and Visualizer both write one per line — and the rule is
confined to prose keys where the result is a cosmetic string rather than a machine value.

**A trailing Tcl comment on a bare value** → `profile_title Espresso ;# tweaked for light roasts`
is ordinary Tcl, and hand-edited profiles exist. First-token gets this right today; rest-of-line
would fold the comment into the title, and Decenza would then re-export that as canonical — the
"self-corrects the ecosystem" argument running in reverse. Mitigation: strip a trailing `;#` or
` #` comment when applying the rule, and test it.

**Divergence from de1app on a title** → Two apps could display different names for the same file
until it is re-saved. Accepted deliberately (see Decisions); the divergence is in the direction of
author intent and is erased the first time Decenza writes the file.

**A promoted dose enabling a recommendation the user did not intend** → Only fires on a non-zero
`profile_grinder_dose_weight`, which only Streamline writes and only from a dose the user
actually set. Absent and zero both leave the recommendation off.

## Migration Plan

No migration. Both changes affect `.tcl` import only, and Decenza's stored profiles are JSON.
Profiles already imported with a truncated title keep it until re-imported or renamed; the same
is true of a dropped dose. Nothing is rewritten in place, consistent with the standing rule
against retro-editing user-saved profiles.

Withdrawal of `preserve-recipe-visualizer-roundtrip` removes its directory with the reason
recorded in this change's tasks; no specs were ever promoted from it.

## Open Questions

None.
