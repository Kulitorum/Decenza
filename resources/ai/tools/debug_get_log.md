# debug_get_log

Reads the persisted app-wide debug log. Four addressing modes, and choosing the wrong one is the
usual reason a search "finds nothing".

| Mode | Call | What it answers |
|---|---|---|
| 0 | `families=true` | A census of this log's line prefixes: how many lines carry each registered marker, each unregistered bracketed prefix, and each bare `ClassName:` prefix, plus how many carry none. It also returns `registeredMarkerCatalog` — every registered marker and what it covers. **Start here** when you do not already know which subsystem you need |
| 1 | `sessions=true` | Every session with its index, start line, timestamp and line count |
| 2 | `session=N` | One session. `-1` is the run happening now, `-2` the previous one, `0` the oldest surviving |
| 3 | (default) | The whole log |

Sessions are listed in the order they were recorded, so index 0 is the OLDEST surviving session.

## A null timestamp means unknown, not "just now"

A session can be reported with `timestamp` null and `startTimeKnown` false. Its lines are intact;
only its start time is unknown. There is more than one cause — the log is capped and trimmed from
the front, which can remove the oldest session's marker, and a marker can also be left
unparseable by a run that ended abruptly — so read `startTimeNote` on that session rather than
assuming which happened.

## Narrowing modes 2 and 3

- `filter` — substring, or a regular expression when `regex` is true. Case-insensitive, applied
  before pagination.
- `minLevel` — DEBUG / INFO / WARN / ERROR / FATAL. App log only (modes 2 and 3).
- `dedupe` — collapses CONSECUTIVE qualifying lines that are identical apart from each line's own
  leading timestamp into one entry carrying `count` and `lastLine`. Non-consecutive repeats stay
  separate.
- `tail` — the last N qualifying (and deduped) entries. Takes precedence over `offset`.

Every returned line carries its absolute line number in the `lines` array.

## Subsystem markers

First-party runtime log lines begin with a bracketed subsystem marker, then optionally their own
source: `[Scale][BLE AcaiaScale] tare sent`. Passing one as `filter` returns that subsystem's
whole narrative — that is what the markers exist for. `families=true` returns the catalog.

**A bracketed marker is a SUBSTRING, not a pattern.** Pass it with `regex` false or absent. Under
`regex: true`, `[Scale]` is a character class matching any line containing S, c, a, l or e — i.e.
almost every line — which looks like a working query returning everything.

**Severity carries audience.** `minLevel: "INFO"` plus a marker is the user-facing story for that
subsystem and nothing else: DEBUG is developer detail (protocol frames, per-poll state), INFO is
the narrative a user may need (lifecycle, discovery outcomes, connect/disconnect, fallback), WARN
and above are problems. That pairing is the same predicate the app's own connections page uses,
so these lines are the ones a user can read on screen — but the page shows only the current
session and only its last few hundred lines, while you are addressing the whole file. Expect to
see more than the user does, not less.

**Older logs may contain many unregistered messages.** A missing catalog marker does not mean
that subsystem was absent. Census `searchWith` fields help recover older class prefixes; those
filters can still miss alternate spellings. Current first-party emitters use registered helpers,
while unattributed/framework messages appear under Runtime with whatever source context Qt
supplied. Runtime formatting does not prove first-party source conformance.

Read the complete unfiltered time window before concluding there were no problems. INFO+
queries omit DEBUG-only evidence, and historical multiline continuations may have no prefix.
BeanBase includes page/archive/provider stages and the terminal extraction/search outcome;
AI covers advice and conversations. A prompt/response file receipt is not a success verdict.

A census describes LINES, not the current build: the log is a ring buffer spanning runs and app
versions, so an unregistered prefix may be one that has since been converted. Pass `session` to
scope a census to one run — unscoped it sums every build that ever wrote to the file, which
cannot show whether a subsystem got quieter.
