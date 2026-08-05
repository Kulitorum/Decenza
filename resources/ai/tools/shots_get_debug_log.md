# shots_get_debug_log

Reads the debug log captured during one shot: BLE frames, phase transitions, stop-at-weight
events, flow calibration, and every qDebug line emitted while the shot ran.

## Narrowing what comes back

- `filter` — substring, or a regular expression when `regex` is true. Case-insensitive. Applied
  before pagination.
- `dedupe` — collapses CONSECUTIVE qualifying lines that are identical apart from a leading
  timestamp into one entry carrying `count` and `lastLine`. Non-consecutive repeats are not
  collapsed.
- `tail` — the last N qualifying (and deduped) entries. Takes precedence over `offset` when both
  are given.
- `minLevel` — accepted but has NO effect here: shot debug lines are not level-tagged. (The
  app-wide `debug_get_log` does honour it.)

Every returned line carries its absolute line number in the `lines` array.
