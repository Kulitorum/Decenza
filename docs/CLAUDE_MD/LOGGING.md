# Logging

How to log so the line is findable later. Read this before adding a log line to a
device, radio or transport subsystem, and before creating a new subsystem.

Decenza's device problems are diagnosed **after the fact, from a log a user
uploaded**, usually with the user's own AI assistant reading it over MCP. You will
not have the hardware, the network, or a second chance to ask. That single fact
drives everything below.

## The two properties every log line must have

**1. One grep returns the whole subsystem.** A line begins with its subsystem's
bracketed marker, then optionally its own source:

```
[Scale][BLE AcaiaScale] Reporting connected (weight frame)
[DE1][Serial] Port opened: cu.usbmodem1234 (115200 8N1)
[Bluetooth][BLEManager] Adapter recovered — re-arming DE1 + scale reconnect
```

So `grep '\[Scale\]'` returns every scale line — drivers, transports, discovery,
USB, WiFi — and nothing else. This only works if it holds for **every** line. One
site with a hand-rolled prefix is invisible to the search *and* the reader cannot
tell it was missed, because the log looks complete. That is why it is
[machine-checked](#enforcement), not merely documented.

**2. Severity carries audience.** Pick a tier by **who needs the line**, not by how
important it feels:

| Tier | Audience | Examples |
|---|---|---|
| `DEBUG` | developers | protocol frames, per-poll state, parse internals, "why this no-op'd" |
| `INFO` | **users** | lifecycle, discovery outcomes, connect/disconnect, transport choice and fallback, scheduling |
| `WARN`+ | problems | failures, timeouts, unreachable peers, rejected data, refused operations |

This is what makes `marker + minLevel INFO` a complete, self-contained answer. The
app's connections-page views run exactly that query, and so does `debug_get_log`, so
**the tier you choose decides whether a user ever sees the line.**

Audience, not authorship: a low-level driver logs INFO when its event is part of the
user-facing story, and a high-level manager logs DEBUG when the detail only serves a
developer. Both directions of mistake are silent — a user-facing line left at DEBUG
vanishes from its view, and driver chatter promoted to INFO puts the firehose back on
screen.

## Adding a log line

Call your subsystem's helper. Never `qDebug()` directly, and never type a marker into
the message.

```cpp
// src/ble/scales/acaiascale.cpp — aliases at the top of the file
#define ACAIA_LOG(msg)  SCALE_LOG("AcaiaScale", msg)
#define ACAIA_INFO(msg) SCALE_INFO("AcaiaScale", msg)
#define ACAIA_WARN(msg) SCALE_WARN("AcaiaScale", msg)

ACAIA_INFO(DECENZA_BLE_MSG_CONNECTED("weight frame"));
ACAIA_LOG(QStringLiteral("Notify enabled on %1").arg(uuid.toString()));
```

The helper headers, one per subsystem:

| Subsystem | Header | Macro family | Note |
|---|---|---|---|
| `[Scale]` | `src/ble/scales/scalelogging.h` | `SCALE_LOG/INFO/WARN` | short form tags `"BLE <prefix>"`; `*_TAGGED` takes the tag verbatim |
| `[DE1]` | `src/ble/de1logging.h` | `DE1_LOG/INFO/WARN_TAGGED` | |
| `[Refractometer]` | `src/ble/refractometers/refractometerlogging.h` | `REFRACTOMETER_LOG/INFO/WARN` | same `"BLE "` short form |
| `[Bluetooth]` | `src/ble/bluetoothlogging.h` | `BT_LOG/INFO/WARN_TAGGED` | **stderr-only by construction** — nothing here has a `logMessage`, so there is no `BT_*_STDERR_TAGGED` and `BT_*_TAGGED` does not emit |

All four families stop at `WARN`. There is no marked CRITICAL/FATAL tier — a genuine
`qCritical` in a covered file has to take an exemption, which is deliberate: nothing
in these subsystems is unrecoverable enough to warrant aborting.

**Alias the macro, never copy its body.** `difluidr1.cpp` and `difluidr2.cpp` each
hand-copied `SCALE_LOG`'s body once, so a one-line fix to the shared macro had to be
found and applied in three places, and the two copies were identical only by luck.

### Which variant

- **`*_TAGGED(tag, msg)`** — the normal form. `tag` names the source and is a string
  literal.
- **`*_STDERR_TAGGED`** — for code with no `logMessage` signal in scope: free
  functions, static helpers, JNI shims, simulators. Also for a `const` member
  function: `DECENZA_SUBSYS_LOG` emits as well as writing, and our `logMessage`
  signals are declared non-const (`de1device.h:361`), so `emit` will not compile
  there. (moc itself is fine with a const signal — it const_casts `this`,
  `qtbase/src/tools/moc/generator.cpp:1297-1300` — so the limit is our declaration.
  This note previously blamed moc, uncited and wrongly.)
- **`*_STDERR_DYN(tag, msg)`** — only when one helper logs on behalf of several
  sources, so a hard-coded tag would name the wrong one. `BLEManager`'s scale and
  refractometer tiers use it because `main.cpp` drives the reconnect ladders and its
  lines must not be stamped `BLEManager`.

### Use the canonical wording for shared events

Thirteen scale drivers and two refractometers report the same handful of events. Use
`DECENZA_BLE_MSG_*` (in `core/logtags.h`) rather than typing the message, so
comparing two models' logs does not start with working out whether "First weight
received, marking as connected" and "Scale confirmed working, reporting connected"
are the same thing. (They were.)

Anything genuinely model-specific — which characteristic, which extra notification —
stays a literal at the call site.

## Three failure modes to avoid

These are the ones that actually happened, repeatedly.

**Don't write the same event twice.** One call per event. The shape to recognise is a
`qDebug()` next to an `emit`/helper call describing the same thing:

```cpp
// WRONG — and it drifts. At 21 USB sites these two ended up describing
// the same event in DIFFERENT WORDS, so neither was redundant and neither
// was complete.
qDebug() << "BLEManager: Direct wake (WiFi) - connecting to" << hostname;
scaleInfo(QStringLiteral("Direct wake (WiFi): connecting to %1").arg(hostname));
```

Put everything in one marked line. If two sinks seem to need different text, the
sinks are the problem, not the wording.

**Don't warn about something that is not wrong.** A `WARN` that fires on a working
configuration trains readers to skim the tier that means "look here". Real examples
removed from this codebase: a successful cache rehydrate warning on *every* launch of
every affected device; "no DE1 found" warning while the user was deliberately running
the simulator. If a retry ladder repeats a genuine failure forever, warn for the
first few and then drop to DEBUG — see `BLEManager::scaleRepeatFailure`, and count
**per message**, because a subsystem-wide counter suppresses a genuinely *new*
failure that arrives after an unrelated one spent the budget.

**Don't report a transition that did not happen.** Guard on state. An unconditional
"disconnected" logs a disconnect for a device that never connected, which reads as
the app having just lost hardware. `DECENZA_BLE_MSG_INCOMPLETE_SUFFIX(ready)` exists
because "the link dropped after working" and "the connect never reached ready" arrive
through the same callback and are different diagnoses.

## Adding a subsystem

Two edits in `src/core/logtags.h`:

1. A `DECENZA_LOG_MARKER_<NAME>` literal.
2. A row in `DECENZA_LOG_SUBSYSTEMS(X)` — marker plus a description **written for
   someone who has never read the code**, because it reaches the `debug_get_log` tool
   description verbatim.

Then a helper header aliasing `DECENZA_SUBSYS_LOG*` for the new marker, following one
of the four above. Do not restate the marker list anywhere else: the MCP description
and `scripts/check_log_markers.py` both derive from the registry, and a copy is free
to drift.

A marker is a **published name**. Renaming one breaks every saved query, filter and
habit built on it. Treat it as API.

**Split a subsystem out when it answers a different question.** `[Refractometer]` is
separate from `[Scale]` even though refractometers run on the scale BLE transports
and appear in the same view, because "my TDS reading is wrong" and "my weight is
wrong" are diagnosed from different lines. `[Bluetooth]` is separate from both because
it sits *beneath* them — when the adapter is wedged neither device can connect, and
filing that under one of them sends a reader hunting a fault in the wrong place.

**Don't let one subsystem claim a shared resource.** One `QBluetoothDeviceDiscoveryAgent`
serves the DE1, the scales and the refractometers. Logging its scan lifecycle under
`[DE1]` made a `[DE1]` filter read "looked for the machine, gave up" when the scan was
actually a WiFi-to-BLE *scale* fallback that succeeded. The event belongs to whoever
asked for it, or to nobody.

## Retrieving a subsystem's story

From a checkout, over a shared log:

```bash
grep '\[Scale\]' debug.log            # the whole scale narrative
grep -E '\[(Scale|Refractometer)\]' debug.log   # what the connections view shows
```

Over MCP, which is how a user's assistant reads it:

```
debug_get_log  session=-1  filter="[Scale]"  minLevel="INFO"
```

**`filter` is a substring — leave `regex` off.** Under `regex: true`, `[Scale]` is a
character class matching any line containing S, c, a, l or e, i.e. nearly every line.
It looks like a working query returning everything.

`session=-1` scopes to the current run. Without it you get every session in the file,
and a scale connecting and disconnecting yesterday looks like it happened just now.

## Where the log goes

`WebDebugLogger` (`src/network/webdebuglogger.h`) installs the Qt message handler,
keeps an in-memory ring buffer for the web poller, and appends every line to
`debug.log` (capped at `MAX_LOG_FILE_SIZE`, trimmed from the front, with a
`========== SESSION START` marker per run).

There is **one** log. The connections page's two views are filtered reads of it via
`WebDebugLogger::sessionLinesMatching()`, and Share sends the same file. If you find
yourself building a second buffer so some screen can show something, you are
recreating the private `scale_debug_log.txt` channel that was deleted: it was capped,
it duplicated lines already on disk, it omitted every other subsystem, and everything
routed *only* through it was absent from every log a user ever submitted.

Adding a view? Use `SubsystemLogView.qml` with a `markers` list. It backfills through
`sessionLinesMatching()` and follows `lineAppended`, and both use the same predicate
so what it shows on arrival matches what a reload shows.

**A slot connected to `lineAppended` must not log.** Doing so re-enters the global
message handler from inside its own emit. There is a per-thread guard against the
recursion, but the guard's cost is dropping that line's signal — so a stray
`console.log` in a view's append handler silently makes the view miss lines.

## Enforcement

`scripts/check_log_markers.py` runs on every PR touching `src/**` (see
`.github/workflows/text-invariants.yml`). No Qt, no compiler, seconds. It parses the
registry rather than restating it, and checks:

1. No bare `qDebug`/`qInfo`/`qWarning`/`qCritical` in a covered file.
2. No registered marker typed into a message (the helper already applies it — typing
   one produces `[Scale] [BLE DecentScaleWifi] …`). Only *registered* tokens, because
   `[M]` is a DE1 protocol byte and `[observe]` a mode qualifier. So an unregistered
   prefix inside a helper call — `SCALE_LOG("Acaia", "[R2-diag] …")` — is caught by
   nothing. Don't do it.
3. No helper header applying a marker the registry does not declare. The header set is
   derived by grepping for the macros, so a new helper is covered the day it lands.
4. No bracketed marker literal in `qml/` that the registry does not declare — the
   views name their subsystems as plain strings, and a rename would otherwise empty a
   view while every other rule passed.

If a line genuinely cannot go through a helper, append
`// log-marker-exempt: <reason>` on or just above it. Give a real reason; the window
is the call line plus six above, precisely so the reason can be a sentence. It must
be a `//` comment — block comments are stripped before the check, so a `/* */`
exemption is invisible — and for rule 2 it must be on the same line.

This is a gate rather than guidance because guidance did not hold. While the markers
were being introduced, **nine** hand-rolled prefix families were found in code already
believed converted — including 37 `DE1Simulator:` lines, all at DEBUG, which left the
connections page's DE1 view **completely empty** on a simulator session. Every one was
found by a person reading a running app's log, not by the tree. A grep finds them in
milliseconds. (The families are gone from `main`, so the count of nine is no longer
checkable from a checkout; it is the tally across commits `460fb8e9` and `3d022dd5`.)

The gate is not the whole invariant, and the difference matters. Rule 1 covers
`COVERED_GLOBS` — `src/ble`, `src/usb`, the two simulator files. Other files carry
subsystem lines and are **not** covered, `src/main.cpp` most of all: it drives the
scale and refractometer reconnect ladders and has ~127 bare log calls. "Every line
carries its marker" is the rule you follow; the gate enforces it where the helpers
live.

## Verify against a running app, not just the source

The last several defects in this area were invisible in review and obvious in one
session's log. Read the real thing:

```
debug_get_log  session=-1  minLevel="INFO"
```

A healthy startup is on the order of 20 INFO+ lines and reads as a narrative. What to
look for: an event appearing twice in different words; a bare marker with no source
tag; a `WARN` on something that is working; a device that reports a state change it
never made; and a subsystem that is **silent when it should not be** — the hardest to
notice, because nothing is there to catch your eye.
