# Malformed `.tcl` fixtures

Real-world profile files that are not valid Tcl, kept verbatim so the import rules
that recover from them are tested against what actually exists rather than against a
hand-written approximation.

## `visualizer_unbraced_title.tcl`

Fetched from `https://visualizer.coffee/api/shots/<id>/profile?format=tcl` on
2026-07-26. Visualizer's `.tcl` renderer *does* brace multi-word values, but only when
they match `/\w\s\w/` — word character, whitespace, word character
(`app/models/shot_information/profile.rb:29`). A title whose every space sits beside a
non-word character misses that test, so `D-Flow / Q` (spaces flanking the `/`) is
written bare while `Damian's Q` is braced correctly:

    profile_title D-Flow / Q

Tcl reads a profile file as a flat key/value list, so `array set` yields
`profile_title` → `D-Flow` plus a stray `/` → `Q`. Verified with `tclsh` against this
exact file. de1app reads it the same way and, because its editor dispatch matches
`[string range $title 0 7]` against the literal `"D-Flow /"`, loses the D-Flow editor
for the profile entirely.

Decenza reads bare values of `profile_title`, `author` and `profile_notes` to the end
of the line instead — see `De1AppTcl::isFreeTextKey`. Visualizer's JSON rendering of
the same profile gets the title right; only the `.tcl` one is affected.

Do not "fix" this file. Its malformedness is the point.
