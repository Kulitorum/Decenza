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
| `[SAW]` | `src/machine/sawlogging.h` | `SAW_{LOG,INFO,WARN}_{TAGGED,STDERR}` | mostly stderr in practice — SAW lives in controllers, a settings store and a worker thread, none of which carry `logMessage` |
| `[Font]` | `src/core/fontlogging.h` | `FONT_{LOG,INFO,WARN}_STDERR` | stderr-only by construction — font setup runs before any object with a `logMessage` exists |
| `[Network]` | `src/core/networklogging.h` | `NETWORK_{LOG,INFO,WARN}_{TAGGED,STDERR}` | reachability only so far — the app's servers still use hand-rolled prefixes |
| `[Screensaver]` | `src/screensaver/screensaverlogging.h` | `SCREENSAVER_{LOG,INFO,WARN}_{TAGGED,STDERR}` | |
| `[Theme]` | `src/core/themelogging.h` | `THEME_{LOG,INFO,WARN}_{TAGGED,STDERR}` | appearance: themes, colours, backgrounds, font SIZES (vs `[Font]`, which is which family resolved) |

All families stop at `WARN`. There is no marked CRITICAL/FATAL tier — a genuine
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

## Four failure modes to avoid

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

**Don't announce an intent as if it were an outcome.** A line that says what the code
is *about to try*, at a tier where the failure of that attempt is not visible, is worse
than silence: it reads as a complete account and the reader draws the wrong conclusion
with nothing to signal that anything is missing.

The WiFi scale did exactly this. At INFO the log said

```
[Scale][BLE DecentScaleWifi] Previous attempt found hds.local unreachable — re-resolving before retry
[Scale][BLE DecentScaleWifi] WebSocket error: Host unreachable — target=192.168.10.145
```

for eight minutes against one unchanging address. The truth was that resolution had
failed and the driver had fallen back to the *cached* address — a line that existed,
at DEBUG. So the narrative asserted a fresh resolve that never happened, and a scale
that had simply moved read as a scale that was switched off.

Two fixes, and prefer the second: defer the line until the branch is known, or put the
outcome on the line that already reports the result. Here the failure line already
printed `target=`; it gained *where that address came from*. Note what the tempting fix
would have cost — promoting the fallback line to INFO adds one line per retry cycle
forever to correct one line that should not have been at INFO. **Fixing a
dishonest line by adding a second line is usually the wrong direction.**

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

**The test is the question, not the hardware.** `[SAW]` is registered even though
stop-at-weight owns no device, because "why did my shot stop where it did" is a
different question from "did the weight readings arrive" — different code, different
fault, and a reader sent to the wrong one wastes the whole investigation. Registration
is open to any subsystem whose lines are retrieved as a group; being a driver is not
the entry requirement. (`#1707` left this open as "shot logic, not a device", which
framed it as a question about ownership. It is not.)

The same test cuts the other way in the same file. `weightprocessor.cpp` is SAW's
worker, but its feed-liveness, stall and interval lines carry **`[Scale]`**, because
they answer whether the readings arrived. Two markers in one file is correct when the
file answers two questions; what is never correct is a third, unregistered prefix —
which is what `[Weight-Worker]` was.

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

## Session boundaries, and what a trim may not do

A `SESSION START` marker asserts **when the session whose lines follow it began**, and
is written only at that moment. Nothing else may write one — in particular no
maintenance of the file, because the only start time such code holds is the *current*
run's, while the lines it would be introducing belong to an older one.

That is not a hypothetical rule. `trimLogFile()` used to re-emit a marker stamped with
the running session's start at the head of the surviving (older) content, "so it
survives the trim". Since the index treats every `SESSION START` as a boundary, the
forgery became a real session in every enumeration:

```
idx 0  3249 lines  2026-07-29T18:17   <- forged by a trim
idx 1  9089 lines  2026-07-28T10:23
idx 2  5526 lines  2026-07-29T08:21
idx 3  2852 lines  2026-07-29T18:17   <- the real one
idx 4  1297 lines  2026-07-30T08:20
```

Two sessions claiming one timestamp, an enumeration not in chronological order, every
`session=N` off by one, and yesterday's lines dated to this morning — the exact hazard
`session=-1` is documented above as protecting you from, reintroduced beneath the
guidance by the thing writing the log.

**A trim writes a banner and nothing else.** The concern the old code named is real —
a trim *can* remove the running session's own marker, if that session alone exceeds
the keep size — and it is handled where it belongs, in the reader: a leading fragment
with no marker of its own is reported as a session with an **unknown** start
(`timestamp: null`, `startTimeKnown: false`), not one borrowed from a neighbour. An
absent timestamp is recoverable by a reader; a wrong one is not.

Two traps if you touch this code:

- The marker is written with a **leading newline**, so line 0 of a perfectly healthy
  fresh log is blank and the marker is on line 1. A headless-fragment test of "line 0
  is not a marker" invents a phantom one-blank-line session on every new log — the
  same defect class. Require a non-blank line before the first marker.
- **And skip the trim banner**, which `trimLogFile()` writes unconditionally. A trim
  landing just before a marker leaves banner-then-marker with nothing orphaned, and
  counting the banner reported a session whose entire content was the banner. That
  shipped in the first cut of this fix and its own tests missed it, because every
  fixture put a real orphaned line after the banner.
- `debug_get_log` reports an unknown start as JSON `null` plus a flag and a reason,
  never as `""`. An empty string reads as a parse failure in the tool and sends the
  reader looking for a bug there instead of understanding that the information was
  destroyed before they arrived.

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

`scripts/check_log_markers.py` runs on **every** PR (see
`.github/workflows/text-invariants.yml`) and is a **required status check** — a red
one blocks the merge button. It used to be path-filtered to `src/**` and friends; the
filter is gone because a required check that is path-filtered never reports at all on
a PR outside the paths, so the PR hangs pending forever. No Qt, no compiler, 12 s. It parses the
registry rather than restating it, and checks:

1. No bare `qDebug`/`qInfo`/`qWarning`/`qCritical` in a **fully** covered file.
2. No registered marker typed into a message (the helper already applies it — typing
   one produces `[Scale] [BLE DecentScaleWifi] …`).
3. No helper header applying a marker the registry does not declare. The header set is
   derived by grepping for the macros, so a new helper is covered the day it lands.
4. No bracketed marker literal in `qml/` that the registry does not declare — the
   views name their subsystems as plain strings, and a rename would otherwise empty a
   view while every other rule passed.
5. **No leading bracketed token that the registry does not declare.** `[Subsystem]` is
   the grammar of a marker and a reader cannot tell `[SAW]` from `[Scale]` by looking,
   so an unregistered one advertises a `debug_get_log` filter that quietly returns an
   incomplete answer — or none — while looking exactly like one that works. Register
   it and give it a helper, or write the prefix so it cannot be mistaken for a marker.

Rule 5 replaces a documented hole. Rule 2 matches only *registered* tokens, so
`SCALE_LOG("Acaia", "[R2-diag] …")` passed rule 1 (it uses the helper) and rule 2
(unregistered) and was caught by nothing. This file used to say so and leave it there.
On its first run rule 5 found **six** unregistered bracketed families
(`[Weight-Worker]`, `[SAW-Worker]`, `[SAW-Latency]`, `[TextRender]`, `[Startup]`,
`[AppState]`) plus seven device lines under a hand-typed `[USB Scale]`/`[BLE DE1]`
that no `[Scale]` or `[DE1]` search returned. A later run, after rule 6 widened the
covered set, found `[Steam]`, `[HW-Tare]`, `[Screensaver]` and `[Theme]` as well.

Two things rule 5 needs in order not to cry wolf, both learned by running it:

- **It only fires on a line that contains a log call.** `m_probeBuffer.contains("[M]")`
  is a protocol comparison, not a message. Leading position alone does not separate a
  log message from any other string literal.
- **The token must start uppercase**, as every registered marker does. `[observe]` is a
  lowercase mode qualifier following a marker the helper already applied; it
  impersonates nothing.

Neither is an allowlist, deliberately — an allowlist of permitted tokens would be a
second registry, free to drift from the first.

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
checkable from a checkout. The reachable citation is the squash-merge `01679eb4`
(#1707); two branch-local hashes cited here earlier were pre-squash objects that no
`git merge-base --is-ancestor` accepts and that vanish on `gc` — a verification path
that does not exist, in the paragraph explaining that the count cannot be verified.)

The gate is not the whole invariant, and the difference matters. There are **two**
coverage sets, because the rules do not all generalise the same way:

- **`COVERED_GLOBS` — all rules.** Files that are *wholly* about their subsystem:
  every log line in `acaiascale.cpp` is a scale line, so "use the helper" is always
  the right instruction.
- **`MARKER_ONLY_GLOBS` — rules 2 and 5 only.** Files that *host* a subsystem's lines
  alongside unrelated code — `main.cpp` is the archetype.

**Both lists live in `scripts/check_log_markers.py`, and are deliberately not copied
here.** This section used to enumerate them and was six entries stale within weeks,
which is the same drift the "do not restate the registry" rule above exists to
prevent. Read the script for membership; read this for the criterion. Rule 6 catches
a file that uses a helper and is in neither list, so nothing depends on either list
being remembered.

The split is a correction, not a concession. `main.cpp` drives both reconnect ladders
*and* initialises fonts, translations, TTS and accessibility; applying rule 1 there
produced 118 "violations" that were overwhelmingly lines with **no subsystem to belong
to**, for which "route it through a helper" has no answer. A check reporting a hundred
non-defects is one people switch off — which is how the generation of this convention
before the gate died. What does hold everywhere is the marker invariant: if you write a
bracketed prefix, it must be registered and applied by its helper. Rules 2 and 5
enforce exactly that, and they are what found the real defects in `main.cpp`.

Still uncovered, and known: **89 distinct** `Class:`-prefixed families across ~1,140
lines in `src/` (44 of them with five or more lines each — the biggest are
`ShotHistoryStorage:` 190, `ShotServer:` 116, `DatabaseBackupManager:` 62), and the bare
`console.log` lines from QML (`Phase Idle/Ready:`, `SteamPage:`). An earlier "roughly 40"
here was only defensible at an unstated five-line cutoff and understated the scope about
twofold; two of its three QML examples were wrong (`FRAME CHANGE:` is C++, in a file now
covered; `Auto flow cal:` does not exist). These have no registry entry and no helper;
covering them would only
teach people to write exemptions. "Every line carries its marker" is the rule you
follow; the gate enforces it where a subsystem has somewhere to log to.

**The gap that is not on that list, because it looks covered:** a *bracketed* family in a
file simply outside both glob sets. Rule 5 is built for exactly that shape and never
sees it. `[FontProbe]` in `src/screensaver/iosbrightness.mm` was four such lines — the
globs reach `src/ble/**/*.mm` but no other `.mm` — and it sat one directory away from a
`[Font]` marker whose registry description promises that a `[Font]` search returns the
font story. It did not. Found in a live session read, not by the gate or by review,
which is the whole argument of the next section. Before adding a directory to the globs,
check what else it contains: `iosbrightness.mm` also hosts `[Screensaver]` and `[Theme]`,
so covering it means registering two more subsystems or writing exemptions, and neither
is free.

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
