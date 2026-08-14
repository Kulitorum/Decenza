## Context

See `proposal.md` — Why, for the measurements.

The primary evidence is the retry census: across 283 retry cycles in the corpus, every write
that recovered did so by retry 9, none is observed to recover at retry 10, and 260 cycles ran
the full budget and failed. That is a direct measurement of this codebase's own behaviour on real
user hardware, and it is what the retry bound rests on.

**On the reference implementations.** An earlier revision of this change justified most of its
decisions by claiming de1app and decaid had independently chosen the opposite policy. That
claim did not survive reading the sources and has been removed. For the record, so it is not
reconstructed later:

- de1app retries **every** DE1 command indefinitely at 500 ms. All 28 `userdata_append` call
  sites in `de1_comms.tcl` pass `vital 1`; the `vital 0` path that drops without retrying is
  used for scale commands. Its author's note at `:183-184` — *"not sure if we should give up
  on the command if it fails, or retry it / retrying a command that will forever fail kind of
  kills the BLE abilities of the app"* — sits **inside** the branch that retries, two lines
  above `after 500 run_next_userdata_cmd`. It is an open question attached to shipped retry
  code, not a policy. de1app also has no write timeout at all, so an un-ACKed write wedges its
  queue permanently with no zombie detection.
- decaid does not turn a write timeout into a reconnect. `write()` rethrows on
  `TimeoutException` with the reason stated in-source: doing otherwise is *"catastrophic mid
  profile-upload … leaving the DE1 stuck 'receiving' (GHC purple)"*. Its
  `_handleBleTimeout` path is reached from GATT-133 and connect failures, and it **retries the
  write** after reconnecting. Its 3-consecutive-timeout detector calls `_declareLinkDead`
  directly and is not gated on the OS probe; the probe corroborates its other triggers.

A later systematic pass over the same two sources — axis by axis, with line citations — did find
real agreement, and it is used in the Decisions below. Where the three agree, it is recorded as
quorum; where they do not, the design says so and rests on Decenza's own measurements instead.
The agreements are: supersede is per-kind and producer-side, never a blanket clear (2/2); an
upload retry can never overlap its predecessor (2/2, by construction in both); and an MMR write
is never verified by read-back (2/2 against — de1app `mmr_write` at `de1_comms.tcl:1086`, decaid
`_mmrWriteRawPermitted` at `unified_de1.mmr.dart:116`; both *do* retry MMR **reads**). The
non-agreements are the per-write retry budget (infinite / 5 / 0) and whether to drain on a
terminal write failure (1-1, and the two situations are not comparable).

de1app's queue-depth warning at 20 (`de1_comms.tcl:49`) stands as the one threshold precedent;
Decenza has no queue-depth signal at all.

**The retry-budget divergence is not a departure from a shared norm.** de1app can retry forever
precisely *because* it has no write timeout, so a retry costs nothing until the BLE stack itself
errors; decaid has a timeout and treats it as fatal to the whole operation. Decenza is the only
one of the three with both a timeout and per-write retries, which is exactly how a worst case
reaches 60.0 s against a 60 s keepalive. The occupancy defect is Decenza-specific, and the
reference apps neither support nor contradict the chosen bound.

**Three constraints shape the scope.**

`writeUrgent()` prepends to `m_commandQueue` when a write is in flight
(`bletransport.cpp:229-232`), and stop-at-weight and sleep route `REQUESTED_STATE` through it
(`de1device.cpp:1186`, `:1235`). This reads as a hazard — a drain discarding a commanded stop —
and an earlier revision of this design asserted it was one. Traced through, it is not: both of
those paths call `clearCommandQueue()` *before* the urgent write (`stopOperationUrgent` calls `clearCommandQueue()` at `de1device.cpp:1137`; `goToSleep` clears the transport queue directly at `:1190`),
and `clearQueue()` sets `m_writePending = false` (`bletransport.cpp:399`), so the urgent write
that follows always takes the direct branch. A stop is never in the queue at the moment of a
clear. The constraint that remains is narrower: a drain must not *start* discarding urgent
entries, because `ensureChargerOn` on app suspend can leave one queued.

The exhaustion signal is consumed elsewhere. `de1LinkFault("write-failed")` feeds the wedge
detector and the scale connection-priority backoff, where a cascade counts as two faults
against a ≥2/60 s threshold on the reasoning that ten retries is several seconds of starvation
(`qtscalebletransport.cpp:391-407`). Shortening the budget makes cascades briefer and more
frequent, which changes that consumer's input in both directions at once. In #1691 a single
exhaustion already latches skip-HIGH for the whole app run.

Verifiability differs sharply by file. `bletransport.cpp`, `de1device.cpp` and
`profilemanager.cpp` are reachable by the suite and already have test files.
`blemanager.cpp` carries 9 `Q_OS_ANDROID` and 12 `Q_OS_IOS` guards, with `setAdapterPower()`
entirely Android-only and no existing test of the recovery path.

## Goals / Non-Goals

**Goals:**

- Stop spending 88% of retry activity on writes that never recover.
- Stop issuing fresh work into a queue still draining doomed writes.
- Make a link that has stopped accepting writes visible in the log.
- Make one machine setting apply consistently.
- Retire an automatic remedy that cannot work, without losing the one real effect it had.

**Non-Goals:**

- **Explaining *why* writes start failing.** Three hypotheses were tested against the corpus
  and all three are dead. Recorded here so they are not re-run:
  - *A burst of user-driven profile uploads.* Recomputed against uploads actually dispatched
    rather than requested, the dose-response inverts — #1466's largest cluster (20
    exhaustions) had the fewest uploads (2) — and #1810, which carries 154 of the corpus's 379
    exhaustions, has one dispatched upload in the 120 s before each of its two largest
    clusters.
  - *The post-reconnect settings dump.* Failure clusters beginning within 90 s of a reconnect:
    1 of 7 in #1466, 0 of 2 in #1586, 0 of 4 in #1691. Only #1810 (2 of 2).
  - *Dual-link contention from a connected scale.* Not answerable from this corpus, by
    construction: almost every user has a BLE scale, so scale-disconnected windows are
    startup slivers rather than a control group, and the scale frequently disconnects
    *because* of the same trouble, so the causality is contaminated. A within-session
    comparison was attempted and abandoned — the one log that appeared to contradict the
    others turned out to be a head-trimmed session whose scale connected before the visible
    window, which the classifier scored as scale-off.

  Also not configuration: the same SM-T503 with the same scale, one app build apart, same
  firmware and same BALANCED latch, ranges from 0.04% (#1424) to 1.9% (#1466).

  This change addresses cost and propagation, both measured. The trigger remains open, and
  §5 and §6 exist partly to answer it: they are reporting-only by design, so a released build
  gathers the write-failure rate and queue depth from the users who never file an issue —
  the population a corpus of bug reports structurally cannot contain.
- **Tearing down any link on the consecutive-failure signal.** Detection and logging only.
  Teardown needs a policy for the DE1 that cannot fire mid-shot, and the scale side needs typed
  transport errors that do not exist yet — `DecentScale::onTransportError()` takes a `QString`
  into which controller errors, service errors and lookup failures are all funnelled.
- **A user-facing surface for that condition.** The obvious route is `bleError`, which
  `qml/main.qml:799-806` deliberately drops when `DE1Device.connected` — and a link in this
  condition is, by construction, connected. Adding a requirement here would contradict an
  existing one in `ble-error-surfacing`. Reconciling them is separate work.
- **Routing all MMR writes through the verified helper.** Two independent reasons, either
  sufficient.

  First, an MMR **read is itself a write**. `sendMMRReadRequest()` issues
  `m_transport->write(READ_FROM_MMR, req)` — a 20-byte write to `a005`, whose properties are
  `0x1a` (write | write-without-response | notify), with the value returning as a
  notification (`de1device.cpp:777-785`). So "verify by read-back" does not add a read to a
  link carrying writes; it adds a **second write**, and `issueMMRReadWithRetry` adds a further
  retry ladder that issues more `a005` writes on timeout. Read-request traffic is already a
  measurable share of the observed failures: of 264 exhaustions in the corpus, **55 are
  `a005`** — MMR read requests failing as writes — against 91 for `a006` (the MMR writes
  themselves). Verification on a contended link therefore worsens the exact condition this
  change exists to reduce.

  Second, `writeMMRVerified` writes with `force=true`, bypassing the dedup cache that exists
  precisely because convergent callers otherwise produce bursts of identical MMR writes
  (`de1device.cpp:1545-1552`, #773) — so it un-suppresses writes that would have been elided
  entirely.

  A verified MMR write can therefore cost two writes plus retries where an unverified one
  costs one write or none. Only the split-assurance defect is in scope, and §7.2 notes the
  evidence argues for levelling that register *down* rather than up.
- **A graduated retry budget.** See below.
- Retiring the adapter power-cycle on SDK ≤ 32.

## Decisions

### A flat retry bound, not a graduated one

The census settles the graduated question, though not as decisively as an earlier revision
claimed. A flat bound of 3 keeps 15 of the 23 observed recoveries; a bound of 5 keeps 17 — a
difference of two cycles out of 283, so either value is defensible and 5 is simply the more
conservative. (The earlier figures, "37 of 54" against "43", were not reproducible from the
corpus; see proposal.md.) What the census does settle is the shape: the failing-link case is
260 of 283 cycles, so a flat bound already spends almost nothing on doomed writes — a link-state-dependent budget would buy the difference between
three retries on a dead link and one, under a second, at the cost of a second concept.
**Alternative considered:** reducing the budget as consecutive failures accumulate, as an
earlier revision proposed. Rejected: no user-felt win, per CLAUDE.md's complexity rule.

Choose the bound in the 3-5 range and check the resulting worst-case elapsed time against the
60 s keepalive period, which is the constraint the current budget violates.

### Recalibrate the fault weighting in the same change

The weighting is specified, not merely coded: `ble-connection-priority` states that a 10-retry
cascade counts as two faults so a single cascade reaches the ≥2/60 s threshold alone. Changing
the budget without re-deriving that leaves a specified constant describing a cascade that no
longer exists, and raises the rate at which a session-wide scale demotion fires. Since BALANCED
-only operation is itself a candidate contributor to write failures, getting this wrong risks a
loop. **Alternative considered:** leaving it and filing a follow-up. Rejected — the coupling is
created by this change.

### Supersede by dropping the previous upload's writes, not by draining the queue

`startProfileUploadTracking()` already detects the supersede (`de1device.cpp:1324-1326`) and
does nothing to the queue; that is the narrowest place to fix the #1466 cascade.

The *mechanism* was changed after reading both reference implementations, which agree with each
other and against the earlier draft of this design. Supersede in de1app is per-kind,
regex-matched on the queue entry's comment, and applied by the producer immediately before it
enqueues the replacement — `remove_matching_ble_queue_entries` (`de1_comms.tcl:1423`), called at
fourteen sites including exactly this case: `{^Espresso header:}` and `{^Espresso frame #}` at
`:1487-88`. decaid reaches the same end a different way, holding `_desiredProfile` /
`_lastPushedProfile` behind a `_generation` guard so a superseded upload is never enqueued at all
(`workflow_device_sync.dart:113-127`). Neither ever clears the queue to supersede something.
Decenza has only the blanket `clearQueue()`, which is why the first draft reached for it.

So: drop the previous upload's pending writes, not everything pending. Pair it with
`m_lastMMRValues` invalidation exactly as `clearCommandQueue()` already does (`:1205-1226`) —
otherwise a discarded MMR write is not delayed but permanently elided by the dedup cache.

**Alternative considered and now rejected: draining on every terminal write failure.** It has no
quorum and no evidence. Decenza today does not drain — it calls `processCommandQueue()` and moves
to the next command (`bletransport.cpp:149`, `:773`) — and de1app does not either, clearing only
on connect and disconnect (`de1_comms.tcl:247-248`, `:629-630`). decaid *does* clear on every
operation timeout, but its queue is the **platform** operation queue, where a stuck entry blocks
every following operation; its own comment says so (`universal_ble_transport.dart:409-415`).
Decenza's queue is app-level, like de1app's, so that reasoning does not carry across. Nothing in
the corpus shows the writes queued behind an abandoned write making anything worse.

### Urgent state writes: an invariant to pin, not a mechanism to build

An earlier draft of this design claimed an unqualified drain could discard a commanded stop,
because `writeUrgent()` prepends into `m_commandQueue` when a write is in flight
(`bletransport.cpp:229-232`). **That overstated the hazard.** Every stop and sleep path calls
`clearCommandQueue()` *first* (`de1device.cpp:1174`, `:1228`), and `clearQueue()` sets
`m_writePending = false` (`bletransport.cpp:402`), so the urgent write that follows always takes
the direct branch and is never in the queue at the moment of the clear. The only urgent write
that can sit queued is `ensureChargerOn` on app suspend, which is not safety-critical.

Neither reference has any priority concept at all — de1app's `de1_send_state` is a plain tail
append (`de1_comms.tcl:1419`) and decaid's `clearQueue` is unconditional — so Decenza already
protects a stop better than both. The requirement stays in the spec because it is a real
invariant and a cheap test, but it is pinned by assertion, not by building a priority queue.

### Gate the upload retry with an in-flight guard

This is the best-supported item in the change: both references make an upload retry that overlaps
its predecessor **structurally impossible**, and Decenza is the only one of the three where it can
happen. In de1app the retry *is* the queue entry, re-run in place (`de1_comms.tcl:167-190`), so
there is no independent retry timer to fire against a busy queue. In decaid an `_uploading` guard
holds the drain loop, and the backoff timer is scheduled only after the previous
`await setProfile(...)` has thrown (`workflow_device_sync.dart:113-116`, `:140-141`, `:177-184`).
Decenza's 1/2/4/8 s ladder (`profilemanager.cpp:239-245`) is a free-running timer that checks
nothing — that is the #1466 stacking.

An in-flight guard is therefore the shape to copy, not the `queueDrained`-plus-backstop scheme
the first draft specified. Both reach the same invariant; the guard reaches it with one concept
instead of three, and without a timeout on a condition that would need its own justification
under the no-timers-as-guards rule.

### Consecutive-failure counting, reported not acted on

The 2→7 gap is the best-evidenced measurement here and it is cheap to count. Acting on it is
where the risk lives, so this change stops at reporting. Note the corpus figure is a **proxy**:
the logs carry no success marker, so runs were computed resetting only at disconnects and
session boundaries, which overestimates. That is an argument for a bound at the low end of the
observed gap, and for instrumenting before acting.

**What decaid does here, and what transfers.** Its equivalent detector is more developed than
this one. Two independent signals feed `_declareLinkDead`: `_maxConsecutiveOpTimeouts = 3`
consecutive GATT timeouts, which force a teardown even when the OS still reports the link
connected; and receiving *our own advertisement* while believing we are connected, throttled to
one probe per 5 s. Both route through an OS connection-state probe that declares the link dead
**only** on an explicit disconnected/disconnecting answer — a probe error is inconclusive and must
never tear down a possibly-live link (`universal_ble_transport.dart:466-483`). The action is to
emit `disconnected` plus a best-effort OS disconnect that releases the GATT handle so it cannot
block the next connect.

Two things transfer: the probe as a confirming step before believing the link is dead, and the
rule that an inconclusive probe changes nothing. The **threshold does not** — decaid counts
operations that carry no per-write retries, whereas Decenza's unit is an *exhausted* write, so
its 3 would be roughly 97 s here. Qt's `QLowEnergyController::state()` is a weaker probe than
`UniversalBle.getConnectionState`, which is a further reason this change stops at reporting.

**Relation to `de1-connection-health`.** That capability already owns the mirror case —
notification liveness, where the link is connected and ACKing writes but has stopped delivering
notifications — and its implementation lives in the same file this counter would
(`bletransport.h:160-176`). These are two halves of one question. They are kept separate here
because that capability's requirements are about teardown-and-reconnect behaviour this change
explicitly does not add; if teardown is added later, they should merge.

## Risks / Trade-offs

**A lower budget abandons writes a longer retry would have landed** → Measured: 17 of 54
observed recoveries occur between retries 5 and 9, so a bound of 5 gives up 11. Against that,
380 cycles currently pay the full cost for nothing. The split-assurance work and existing
profile-ACK verification mean the writes that matter most are checked.

**Shortening the budget changes a consumer's input** → Addressed directly by the
`ble-connection-priority` delta rather than left implicit.

**A selective drain is more complex than an unconditional one** → It is also the difference
between discarding a doomed profile frame and discarding a stop command. Not optional.

**The consecutive bound may be too low or too high** → It only produces a log line in this
change, so a wrong bound costs log noise, not behaviour. That is the point of stopping at
reporting.

**`blemanager.cpp` changes are compiled only by Android CI** → Keep the diff to the adapter-leg
gating plus the named re-arm caller, and dispatch an Android build on the branch before merge.

## Migration Plan

No data or schema migration; no persisted state introduced. Every element is independently
revertable and rollback is a branch revert.

Validation order: suite via the Qt Creator MCP; `android-release.yml` on the branch so the
`Q_OS_ANDROID` paths compile; `linux-release.yml` for the suite in CI; then a beta build to the
#1810, #1691 and SM-T503 reporters.

## Open Questions

- Whether the bound lands at 3 or 5. The census gives the shape (nothing recovers past 9, most
  recoveries are early) but the cost of giving up the 5-9 band is a judgement about which
  writes those were; the logs do not record which characteristic recovered at which retry.
