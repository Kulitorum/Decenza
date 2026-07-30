# Make the log diagnosable by the reader it is actually written for

## Why

Decenza's device problems are diagnosed after the fact, from a log a user
uploaded, usually by the user's own AI assistant reading it over MCP. #1716
brought nine subsystems under a marker registry and made one grep return each
one's whole story. Measuring the result against that reader — rather than
against the registry — shows the registry was never the binding constraint.

Measured over a real 23,167-line file (21,723 parsed lines, 75 sessions):

| | |
|---|---|
| DEBUG / WARN / INFO | 18,493 (85%) / 2,238 / 992 |
| `Class:` prefix | 35.8% |
| Registered marker | 27.7% |
| Bracketed, unregistered | 23.2% |
| No attribution at all | 13.3% |
| Distinct families | 86 |

(The file spans pre- and post-#1716 binaries, so the unregistered-bracket share
is inflated by historical lines. The shape of the problem is not.)

**Benchmarked against a real user-submitted log** (25,720 lines, 4 sessions,
attached to #1713 — a report that grind settings show whole numbers only). That
benchmark changed the conclusion, and is why this proposal does NOT propose
converting the remaining families:

- **The reported bug is undiagnosable from the log it came with.** `grind`
  appears three times in 25,720 lines and not one reports the step in use. The
  single relevant line carries no prefix at all, so nothing leads a reader to it.
- **Device/BLE/memory/screensaver: 19,073 lines. Recipe/grind/settings/UI: 26.**
  The log is a device log; the bug was not a device bug. Adding a marker over 26
  lines would repeat #1716's `[Network]` defect exactly — a description
  promising a story the marker does not carry. **The gap is lines, not markers.**
- **1,670 of the log's 1,979 warnings are one repeated message** ("Scale
  connection timeout - not found"), which is 84% of all WARN and is why WARN
  outnumbers INFO 12:1. #1716 already routes it through the repeat budget, so
  this collapses without further work — recorded here because it is the
  measurement that shows the budget was worth building.

Four obstacles, in the order they cost a diagnosis:

**1. The reader cannot know what it cannot see.** `debug_get_log`'s description
is generated from the registry, so it names nine subsystems and is silent about
the other 77 families. An assistant searches `[Scale]`, gets a complete and
correct answer, and reasonably infers the log is marker-organised. Nothing tells
it MqttClient (1,956 lines), ShotServer (782), LocationProvider (696) or
BatteryManager (274) exist. This is not a missing-marker problem — it is a tool
advertising a grammar that covers 28% of the log while saying nothing about the
rest, which is the difference between an unknown and an unknown unknown.

**2. No line carries a wall clock.** `[ 477.771]` is seconds since app start.
Shots, bags and every other record carry ISO timestamps. Joining "this shot" to
"these lines" therefore means locating the session marker, parsing its ISO
start, and adding an offset — arithmetic an assistant does badly and states
confidently. Every line *looks* timestamped, so the gap is invisible until a
conclusion is already wrong.

**3. 85% of the log is DEBUG, and DEBUG is the default.** A `debug_get_log` call
with no `minLevel` returns the firehose.

**4. One diagnostic emits 561 lines from a single call.**
`TranslationManager`'s post-scan report joins 560 registry keys into one
`qDebug()` with embedded newlines. Only the first line carries a timestamp and
level; the other 560 are unattributable, defeat line-based parsing, and exceed
the entire 500-line ring buffer on their own. It fires for any user who opens
the Language settings tab.

## What Changes

- **Absolute time REPLACES elapsed seconds** in the line prefix
  (`[11:44:52.310]`, not `[ 477.771]`). Both are derivable from the other given
  the session marker, so this is about which direction costs the reader
  arithmetic, and the join to a shot's ISO timestamp is the one actually asked
  for. Kept inside one bracket so every existing parser — and every log already
  on disk — still reads.
- **Honest coverage.** `debug_get_log` reports which families are registered AND
  which are not, so an unregistered subsystem is a known gap rather than an
  invisible one. This is deliberately independent of converting anything: the
  disclosure is worth having whether or not a family ever becomes a marker.
- **The 560-key dump loses its enumeration.** The count is a real signal and
  stays; the list is regenerable on demand and helps nobody reading a device
  log.
- **No new markers.** This is a reversal, and the user-log benchmark is why: the
  device story is already complete on `main` after #1716, and the domains where
  user-reported bugs actually live have almost no lines to mark. A marker over
  26 lines would repeat the `[Network]` defect. Where a domain is under-logged,
  the answer is to log the decision that determines the behaviour — as done for
  #1713 — not to label the silence.
- **#1713 fixed, and the line that would have diagnosed it added.** The grind
  step is derived from the user's own shot history and falls back when history
  is too thin; the fallback was a flat `1.0`, so a user with one recorded
  setting got whole numbers and could not reach 1.2 at all. It is now 0.1 for
  numeric grinders (1.0 kept for click-indexed ones, whose units are discrete).
  This is included here rather than as its own change because it is the
  benchmark that produced the conclusions above — see Why.
- **The small honesty defects carried over from #1716's deferred list**:
  `BatteryManager`'s "cycle 1 of 5" that never reaches cycle 2, an unattributed
  `QSslSocket: device not open`, `McpRemoteAccess` rejections that never say
  what was rejected, steam events logged from two places, float noise in
  payloads, and `ScaleDevice`'s `DISCONNECTED` at WARN on an expected
  disconnect.

## Impact

- Affected specs: `device-log-views`, `log-tagging-convention`
- Affected code: `src/network/webdebuglogger.{h,cpp}`, `src/mcp/mcplogfilter.h`,
  `src/mcp/mcpresources.cpp`, `src/core/translationmanager.cpp`,
  `src/history/shothistorystorage{,_queries}.{h,cpp}`,
  `qml/components/GrindRowSource.qml`.
- **Line-format change**: the prefix is now `[HH:mm:ss.zzz]` rather than elapsed
  seconds. One bracket, so `lineLevel()` and `stripTimestampPrefix()` are
  unaffected and mixed-format files parse.
- Not in scope: converting the remaining families (now argued against rather
  than merely deferred), and the QML `console.log` lines, which need a QML-side
  helper that does not exist.
